import { createRequire } from 'node:module'
import { isAbsolute, resolve } from 'node:path'
import { pathToFileURL } from 'node:url'

import { type SuiteCutAudioPlugin } from './audio-plugin.js'
import { type SuiteCutAudioPluginReference, SuiteCutAudioPluginReferenceSchema } from './schemas.js'
import { type UntrustedInput } from './untrusted.js'

const BUILT_IN_AUDIO_PROVIDERS = new Set(['kokoro', 'macos-say'])

function normalizeModuleSpecifier(specifier: string, baseDirectory: string): string {
  if (specifier.startsWith('file:')) return new URL(specifier).href
  if (isAbsolute(specifier)) return pathToFileURL(specifier).href
  if (specifier.startsWith('.')) return pathToFileURL(resolve(baseDirectory, specifier)).href
  if (/^[a-z][a-z0-9+.-]*:/u.test(specifier)) {
    throw new Error(`SuiteCut audio plugin modules cannot use this URL scheme: ${specifier}`)
  }
  const requireFromProject = createRequire(pathToFileURL(resolve(baseDirectory, 'package.json')))
  return pathToFileURL(requireFromProject.resolve(specifier)).href
}

/** Parses plugin references, rejects collisions, and resolves local module paths. */
export function resolveAudioPluginReferences(
  references: readonly SuiteCutAudioPluginReference[],
  baseDirectory = process.cwd(),
): ReadonlyMap<string, SuiteCutAudioPluginReference> {
  const resolved = new Map<string, SuiteCutAudioPluginReference>()
  for (const input of references) {
    const reference = SuiteCutAudioPluginReferenceSchema.parse(input)
    if (BUILT_IN_AUDIO_PROVIDERS.has(reference.provider)) {
      throw new Error(
        `SuiteCut audio plugins cannot replace built-in provider ${reference.provider}`,
      )
    }
    if (resolved.has(reference.provider)) {
      throw new Error(`Duplicate SuiteCut audio plugin provider: ${reference.provider}`)
    }
    resolved.set(reference.provider, {
      provider: reference.provider,
      module: normalizeModuleSpecifier(reference.module, baseDirectory),
      ...(reference.options === undefined ? {} : { options: reference.options }),
    })
  }
  return resolved
}

function moduleDefault(module: UntrustedInput): UntrustedInput {
  if (typeof module !== 'object' || module === null || !('default' in module)) {
    throw new Error('SuiteCut audio plugin module must have a default export')
  }
  return module.default as UntrustedInput
}

function parseAudioPlugin(input: UntrustedInput): SuiteCutAudioPlugin {
  if (typeof input !== 'object' || input === null || !('synthesize' in input)) {
    throw new Error('SuiteCut audio plugin default export must define synthesize()')
  }
  if (typeof input.synthesize !== 'function') {
    throw new Error('SuiteCut audio plugin synthesize export must be a function')
  }
  if ('dispose' in input && input.dispose !== undefined && typeof input.dispose !== 'function') {
    throw new Error('SuiteCut audio plugin dispose export must be a function')
  }
  return input as SuiteCutAudioPlugin
}

/** Loads and checks one external plugin module inside the narration worker. */
export async function loadAudioPlugin(
  reference: SuiteCutAudioPluginReference,
): Promise<SuiteCutAudioPlugin> {
  const parsed = SuiteCutAudioPluginReferenceSchema.parse(reference)
  const imported = (await import(parsed.module)) as UntrustedInput
  return parseAudioPlugin(moduleDefault(imported))
}
