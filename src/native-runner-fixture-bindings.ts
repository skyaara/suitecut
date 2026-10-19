// SPDX-License-Identifier: MIT
// Declaration serialization and native mapping validation only; no JS planning.
import { Buffer } from 'node:buffer'

import {
  nativeFixtureDeclarationLayers,
  type NativeFixtureDeclarations,
  type NativeFixtureLayers,
} from './native-fixture-layers.js'
import { nativeFixtureLocation } from './native-fixture-location.js'
import {
  nativeFixtureLiteralWire,
  type NativeFixtureLiteral,
  type NativeFixtureLiteralWire,
  type NativeSelectableFixtureDefinition,
} from './native-fixture-values.js'

type Scope = 'test' | 'worker'
type Binding = readonly [name: string, key: string]
interface Version {
  readonly definition: NativeSelectableFixtureDefinition
  readonly scope: Scope
  readonly bindings: readonly Binding[]
  readonly constant?: Constant
}
interface Constant {
  readonly declarationId: string
  readonly fixtureKey: string
  readonly origin: 'declaredLiteral' | 'configuredLiteral'
  readonly value: NativeFixtureLiteral
}
const validName = (name: string): boolean => /^[A-Za-z_][A-Za-z0-9_]{0,255}$/u.test(name)
const wireFields = (wire: NativeFixtureLiteralWire): readonly string[] =>
  wire.kind === 'undefined' ? ['U', '-'] : ['J', Buffer.from(wire.json).toString('hex')]

/** The executable supplies every effective root and dependency target. */
export class NativeFixtureBindings {
  private readonly declarations: NativeSelectableFixtureDefinition[] = []
  private readonly names: string[] = []
  private readonly allNames = new Set<string>()
  private readonly versions = new Map<string, Version>()
  private readonly roots = new Map<string, string>()
  private readonly layers: readonly NativeFixtureDeclarations[]
  private readonly options = new Map<string, NativeFixtureLiteralWire>()
  private readonly constants = new Map<string, Constant>()
  private ready = false

  constructor(
    catalog: NativeFixtureLayers | NativeFixtureDeclarations,
    options: Readonly<Record<string, NativeFixtureLiteral>> = {},
  ) {
    this.layers = nativeFixtureDeclarationLayers(catalog)
    let declarationBytes = 0,
      valueBytes = 0,
      edges = 0
    for (const layer of this.layers)
      for (const [name, definition] of Object.entries(layer)) {
        declarationBytes += name.length + 128
        valueBytes += name.length + 128
        if ('value' in definition) {
          const wire = nativeFixtureLiteralWire(definition.value)
          valueBytes += 32 + (wire.kind === 'json' ? Buffer.byteLength(wire.json) : 0)
        }
        for (const dependency of 'setup' in definition ? (definition.dependencies ?? []) : []) {
          edges++
          declarationBytes += dependency.length + 16
        }
        if (
          this.declarations.length >= 256 ||
          edges > 4096 ||
          declarationBytes > 2 * 1024 * 1024 ||
          valueBytes > 2 * 1024 * 1024
        )
          throw new RangeError('Native raw fixture declaration capacity exceeded')
        this.names.push(name)
        this.allNames.add(name)
        this.declarations.push(definition)
      }
    if (Object.keys(options).length > 256)
      throw new Error('Native fixture option capacity256 exceeded')
    let bytes = valueBytes
    for (const [name, value] of Object.entries(options)) {
      if (!validName(name)) throw new Error('Invalid native fixture option name')
      const wire = nativeFixtureLiteralWire(value)
      bytes += name.length + 64 + (wire.kind === 'json' ? Buffer.byteLength(wire.json) : 0)
      if (bytes > 2 * 1024 * 1024)
        throw new Error('Native fixture option metadata capacity exceeded')
      this.options.set(name, wire)
    }
  }
  rows(): readonly (readonly (string | number)[])[] {
    let id = 0
    const rows: (readonly (string | number)[])[] = []
    for (const [index, layer] of this.layers.entries()) {
      rows.push(['FLAYER', index + 1])
      for (const [name, definition] of Object.entries(layer)) {
        const dependencies = 'setup' in definition ? definition.dependencies : undefined
        rows.push([
          'FDECL',
          ++id,
          name,
          definition.scope ?? '-',
          definition.automatic === undefined ? '-' : definition.automatic ? 1 : 0,
          dependencies?.length ? dependencies.join(',') : '-',
        ])
        if (definition.timeout !== undefined) rows.push(['FTIMEOUT', id, definition.timeout])
        if (definition.title !== undefined)
          rows.push(['FTITLE', id, Buffer.from(definition.title, 'utf16le').toString('hex') || '-'])
        const location = nativeFixtureLocation(definition)
        if (location)
          rows.push([
            'FLOCATION',
            id,
            Buffer.from(location.file).toString('hex'),
            location.line,
            location.column,
          ])
        if (definition.option !== undefined) rows.push(['FOPTION', id, definition.option ? 1 : 0])
        if ('value' in definition)
          rows.push(['FLITERAL', id, ...wireFields(nativeFixtureLiteralWire(definition.value))])
      }
    }
    for (const [name, wire] of this.options) rows.push(['FCONFIG', name, ...wireFields(wire)])
    return rows
  }
  packet(fields: readonly string[]): boolean {
    const command = fields[0]
    if (
      command !== 'FCONST' &&
      command !== 'FVERSION' &&
      command !== 'FROOT' &&
      command !== 'FPLAN_READY'
    )
      return false
    if (this.ready) throw new Error('Native fixture mappings are already admitted')
    if (command === 'FCONST') {
      const [, rawId, key, kind, origin, encoded] = fields
      const id = Number(rawId),
        definition = this.declarations[id - 1]
      if (
        fields.length !== 6 ||
        !rawId ||
        !/^[1-9][0-9]{0,2}$/u.test(rawId) ||
        !definition ||
        key !== `nativeFixture${id}` ||
        this.versions.has(key) ||
        this.constants.has(key)
      )
        throw new Error('Invalid native fixture constant identity')
      const expected =
        origin === 'L' && 'value' in definition
          ? nativeFixtureLiteralWire(definition.value)
          : origin === 'C'
            ? this.options.get(this.names[id - 1] ?? '')
            : undefined
      if (!expected || wireFields(expected)[0] !== kind || wireFields(expected)[1] !== encoded)
        throw new Error('Native fixture constant differs from actual declared/configured value')
      this.constants.set(key, {
        declarationId: rawId,
        fixtureKey: key,
        origin: origin === 'L' ? 'declaredLiteral' : 'configuredLiteral',
        value:
          expected.kind === 'undefined'
            ? undefined
            : (JSON.parse(expected.json) as NativeFixtureLiteral),
      })
    } else if (command === 'FVERSION') {
      const [, rawId, key, scope, aliases] = fields
      if (fields.length !== 5 || !rawId || !/^[1-9][0-9]{0,2}$/u.test(rawId))
        throw new Error('Invalid native fixture version identity')
      const id = Number(rawId),
        definition = this.declarations[id - 1]
      if (
        !definition ||
        key !== `nativeFixture${id}` ||
        this.versions.has(key) ||
        (scope !== 'test' && scope !== 'worker') ||
        (definition.scope !== undefined && definition.scope !== scope) ||
        aliases === undefined
      )
        throw new Error('Native fixture version differs from actual declaration')
      const bindings: Binding[] =
        aliases === '-'
          ? []
          : aliases.split(',').map((alias) => {
              const [name, target, extra] = alias.split(':')
              if (
                !name ||
                !validName(name) ||
                !target ||
                !/^nativeFixture[1-9][0-9]{0,2}$/u.test(target) ||
                extra !== undefined
              )
                throw new Error('Invalid native fixture dependency alias')
              return [name, target] as const
            })
      const constant = this.constants.get(key)
      if (!constant && 'value' in definition)
        throw new Error('Literal fixture lacks actual native constant selection')
      const actual = constant
        ? []
        : [...new Set('setup' in definition ? (definition.dependencies ?? []) : [])]
      if (
        bindings.length !== actual.length ||
        bindings.some(([name], index) => name !== actual[index])
      )
        throw new Error('Native fixture aliases differ from actual callback dependencies')
      this.versions.set(key, { definition, scope, bindings, ...(constant ? { constant } : {}) })
    } else if (command === 'FROOT') {
      const [, name, key] = fields
      if (
        fields.length !== 3 ||
        !name ||
        !this.allNames.has(name) ||
        !key ||
        this.roots.has(name) ||
        this.names[Number(key.slice('nativeFixture'.length)) - 1] !== name ||
        !this.versions.has(key)
      )
        throw new Error('Invalid native effective fixture root')
      this.roots.set(name, key)
    } else {
      if (
        fields.length !== 3 ||
        fields[1] !== String(this.versions.size) ||
        fields[2] !== String(this.roots.size) ||
        this.roots.size !== this.allNames.size
      )
        throw new Error('Native fixture mapping catalog is incomplete')
      for (const version of this.versions.values())
        for (const [, target] of version.bindings)
          if (!this.versions.has(target))
            throw new Error('Native fixture dependency target was not admitted')
      if ([...this.constants.keys()].some((key) => !this.versions.has(key)))
        throw new Error('Native selected constant has no admitted fixture version')
      this.ready = true
    }
    return true
  }
  version(key: string): Version {
    const version = this.versions.get(key)
    if (!this.ready || !version) throw new Error('Native fixture callback has no admitted version')
    return version
  }
  root(name: string): string {
    const key = this.roots.get(name)
    if (!this.ready || !key) throw new Error('Native fixture name has no admitted effective root')
    return key
  }
}
