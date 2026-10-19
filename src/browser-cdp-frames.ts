// SPDX-License-Identifier: MIT
import { type DriverFrame } from './browser-protocol-types.js'
import { type BrowserWire, protocolNumber, protocolRecord, protocolString } from './browser-wire.js'
import { type UntrustedInput } from './untrusted.js'

interface FrameState extends DriverFrame {
  sessionId: string
  contextId?: number
}
interface SessionState {
  pageId: string
  frameId?: string
  ready: Promise<void>
  parentSessionId?: string
}

/** Active frames and default worlds only; navigation/detach never retain old worlds. */
export function createCDPFrames(wire: BrowserWire) {
  const frames = new Map<string, FrameState>()
  const sessions = new Map<string, SessionState>()
  const roots = new Map<string, string>()
  const removers: (() => void)[] = []
  const changes = new Map<string, Set<() => void>>()
  function changed(frameId: string): void {
    const listeners = changes.get(frameId)
    changes.delete(frameId)
    if (listeners) for (const listener of listeners) listener()
  }
  function waitForChange(frameId: string, deadline: number): Promise<void> {
    return new Promise((resolve) => {
      const listeners = changes.get(frameId) ?? new Set<() => void>()
      const done = () => {
        clearTimeout(timer)
        listeners.delete(done)
        if (!listeners.size && changes.get(frameId) === listeners) changes.delete(frameId)
        resolve()
      }
      const timer = setTimeout(done, Math.max(1, deadline - performance.now()))
      listeners.add(done)
      changes.set(frameId, listeners)
    })
  }

  function removeFrame(id: string): void {
    for (const frame of frames.values()) if (frame.parentId === id) removeFrame(frame.id)
    frames.delete(id)
    changed(id)
  }
  function remember(input: UntrustedInput, sessionId: string, parentId?: string): void {
    const owner = sessions.get(sessionId)
    if (!owner) return
    const value = protocolRecord(input, 'frame')
    const id = protocolString(value.id, 'frame id')
    const previous = frames.get(id)
    const explicitParent = typeof value.parentId === 'string' ? value.parentId : parentId
    const parent = explicitParent ?? previous?.parentId
    // The root renderer also reports OOPIF nodes. Keep their attached renderer ownership.
    const frameSession =
      previous && sessions.get(previous.sessionId)?.frameId === id ? previous.sessionId : sessionId
    const frame: FrameState = {
      id,
      pageId: owner.pageId,
      url: typeof value.url === 'string' ? value.url : (previous?.url ?? ''),
      sessionId: frameSession,
      ...(typeof value.name === 'string' ? { name: value.name } : {}),
      ...(parent ? { parentId: parent } : {}),
      ...(previous?.contextId !== undefined ? { contextId: previous.contextId } : {}),
    }
    frames.set(id, frame)
    changed(id)
    if (!frame.parentId || (frameSession === sessionId && owner.frameId === id)) owner.frameId = id
  }
  function rememberTree(input: UntrustedInput, sessionId: string, parentId?: string): void {
    if (!input || typeof input !== 'object' || Array.isArray(input)) return
    const tree = protocolRecord(input, 'frame tree')
    remember(tree.frame, sessionId, parentId)
    const id = protocolString(protocolRecord(tree.frame, 'frame tree node').id, 'frame id')
    if (Array.isArray(tree.childFrames))
      for (const child of tree.childFrames as UntrustedInput[]) rememberTree(child, sessionId, id)
  }
  async function refresh(sessionId: string): Promise<void> {
    const tree = await wire.request('Page.getFrameTree', {}, { sessionId })
    if (sessions.has(sessionId) && tree.frameTree) rememberTree(tree.frameTree, sessionId)
  }
  function register(
    pageId: string,
    sessionId: string,
    frameId?: string,
    parentSessionId?: string,
  ): Promise<void> {
    const previous = sessions.get(sessionId)
    if (previous) return previous.ready
    const state: SessionState = {
      pageId,
      ...(frameId ? { frameId } : {}),
      ...(parentSessionId ? { parentSessionId } : {}),
      ready: Promise.resolve(),
    }
    sessions.set(sessionId, state)
    if (frameId) {
      const frame = frames.get(frameId)
      frames.set(frameId, {
        id: frameId,
        pageId,
        url: frame?.url ?? '',
        sessionId,
        ...(frame?.parentId ? { parentId: frame.parentId } : {}),
        ...(frame?.name ? { name: frame.name } : {}),
      })
      changed(frameId)
    } else roots.set(pageId, sessionId)
    state.ready = (async () => {
      if (frameId) await wire.request('Page.enable', {}, { sessionId })
      await Promise.all([
        wire.request('Runtime.enable', {}, { sessionId }),
        wire.request(
          'Target.setAutoAttach',
          {
            autoAttach: true,
            waitForDebuggerOnStart: false,
            flatten: true,
            filter: [{ type: 'iframe', exclude: false }, { exclude: true }],
          },
          { sessionId },
        ),
        refresh(sessionId),
      ])
    })()
    void state.ready.catch(() => undefined)
    return state.ready
  }
  function removeSession(sessionId: string): void {
    const owner = sessions.get(sessionId)
    sessions.delete(sessionId)
    for (const [childId, child] of sessions)
      if (child.parentSessionId === sessionId) removeSession(childId)
    if (owner && roots.get(owner.pageId) === sessionId) roots.delete(owner.pageId)
    for (const frame of frames.values()) if (frame.sessionId === sessionId) removeFrame(frame.id)
  }
  removers.push(
    wire.on('Target.attachedToTarget', (event, parentSessionId) => {
      const parent = parentSessionId ? sessions.get(parentSessionId) : undefined
      if (!parent || typeof event.sessionId !== 'string') return
      const info = protocolRecord(event.targetInfo, 'attached frame target')
      if (info.type !== 'iframe' || typeof info.targetId !== 'string') return
      void register(parent.pageId, event.sessionId, info.targetId, parentSessionId).catch(
        () => undefined,
      )
    }),
  )
  removers.push(
    wire.on('Target.detachedFromTarget', (event) => {
      if (typeof event.sessionId === 'string') removeSession(event.sessionId)
    }),
  )
  removers.push(
    wire.on('Page.frameAttached', (event, sessionId) => {
      if (!sessionId || typeof event.frameId !== 'string') return
      remember({ id: event.frameId, parentId: event.parentFrameId }, sessionId)
    }),
  )
  removers.push(
    wire.on('Page.frameNavigated', (event, sessionId) => {
      if (!sessionId) return
      const info = protocolRecord(event.frame, 'navigated frame')
      if (typeof info.id === 'string') {
        const old = frames.get(info.id)
        if (old?.sessionId === sessionId) {
          delete old.contextId
          for (const frame of frames.values()) if (frame.parentId === old.id) removeFrame(frame.id)
        }
      }
      remember(info, sessionId)
    }),
  )
  removers.push(
    wire.on('Page.frameDetached', (event, sessionId) => {
      if (typeof event.frameId !== 'string') return
      if (event.reason === 'swap') {
        const frame = frames.get(event.frameId)
        // A parent's process-swap event may follow the new renderer's context.
        if (sessionId && frame?.sessionId === sessionId) delete frame.contextId
      } else removeFrame(event.frameId)
    }),
  )
  removers.push(
    wire.on('Runtime.executionContextCreated', (event, sessionId) => {
      if (!sessionId) return
      const owner = sessions.get(sessionId)
      if (!owner) return
      const context = protocolRecord(event.context, 'execution context')
      if (typeof context.id !== 'number' || !context.auxData) return
      const aux = protocolRecord(context.auxData, 'execution context auxiliary data')
      if (aux.isDefault !== true || typeof aux.frameId !== 'string') return
      const frame = frames.get(aux.frameId)
      if (
        frame &&
        frame.sessionId !== sessionId &&
        sessions.get(frame.sessionId)?.frameId === frame.id
      )
        return
      if (frame) {
        frame.sessionId = sessionId
        frame.contextId = context.id
      } else
        frames.set(aux.frameId, {
          id: aux.frameId,
          pageId: owner.pageId,
          url: '',
          sessionId,
          contextId: context.id,
        })
      changed(aux.frameId)
    }),
  )
  removers.push(
    wire.on('Runtime.executionContextDestroyed', (event, sessionId) => {
      for (const frame of frames.values())
        if (frame.sessionId === sessionId && frame.contextId === event.executionContextId)
          delete frame.contextId
    }),
  )
  removers.push(
    wire.on('Runtime.executionContextsCleared', (_event, sessionId) => {
      for (const frame of frames.values()) if (frame.sessionId === sessionId) delete frame.contextId
    }),
  )

  async function route(pageId: string, frameId?: string) {
    const root = roots.get(pageId)
    if (!root) throw new Error('Browser page session is not prepared')
    if (!frameId) return { sessionId: root }
    const deadline = performance.now() + wire.timeoutMs
    for (;;) {
      let frame = frames.get(frameId)
      if (frame?.pageId !== pageId) throw new Error('Unknown or detached browser frame')
      await sessions.get(frame.sessionId)?.ready
      frame = frames.get(frameId)
      if (frame?.pageId !== pageId || !sessions.has(frame.sessionId))
        throw new Error('Browser frame detached')
      if (frame.contextId !== undefined)
        return { sessionId: frame.sessionId, contextId: frame.contextId }
      // OOPIF auto-attach and navigation contexts can follow the parent's load event.
      // Subscribe to lifecycle changes instead of polling or creating an isolated world.
      if (performance.now() >= deadline)
        throw new Error('Browser frame execution context is unavailable')
      await waitForChange(frameId, deadline)
    }
  }
  function info(pageId: string, frameId: string): DriverFrame | undefined {
    const frame = frames.get(frameId)
    if (frame?.pageId !== pageId) return undefined
    return {
      id: frame.id,
      pageId: frame.pageId,
      url: frame.url,
      ...(frame.parentId ? { parentId: frame.parentId } : {}),
      ...(frame.name !== undefined ? { name: frame.name } : {}),
    }
  }
  return {
    register,
    route,
    info,
    removeSession,
    pageForFrame: (frameId: string) => frames.get(frameId)?.pageId,
    pageForSession: (sessionId: string) => sessions.get(sessionId)?.pageId,
    async list(pageId: string): Promise<DriverFrame[]> {
      const root = roots.get(pageId)
      if (root) await sessions.get(root)?.ready
      const mainId = root ? sessions.get(root)?.frameId : undefined
      return [...frames.keys()]
        .flatMap((id) => {
          const frame = info(pageId, id)
          return frame ? [frame] : []
        })
        .sort((a, b) => Number(b.id === mainId) - Number(a.id === mainId))
    },
    async resolve(pageId: string, expression: string, parentFrameId?: string): Promise<string> {
      const { sessionId, contextId } = await route(pageId, parentFrameId)
      const result = await wire.request(
        'Runtime.evaluate',
        {
          expression,
          returnByValue: false,
          awaitPromise: true,
          ...(contextId !== undefined ? { contextId } : {}),
        },
        { sessionId },
      )
      const remote = protocolRecord(result.result, 'frame element')
      if (typeof remote.objectId !== 'string')
        throw new Error('Frame locator did not resolve to an iframe element')
      try {
        if (result.exceptionDetails || remote.subtype !== 'node')
          throw new Error('Frame locator did not resolve to an iframe element')
        const described = await wire.request(
          'DOM.describeNode',
          { objectId: remote.objectId },
          { sessionId },
        )
        const node = protocolRecord(described.node, 'frame element node')
        if (node.nodeName !== 'IFRAME' && node.nodeName !== 'FRAME')
          throw new Error('Frame locator resolved to an element that is not an iframe')
        const id = protocolString(node.frameId, 'iframe frame id')
        if (!info(pageId, id)) await refresh(sessionId)
        if (!info(pageId, id)) throw new Error('Browser frame detached while resolving element')
        return id
      } finally {
        await wire
          .request('Runtime.releaseObject', { objectId: remote.objectId }, { sessionId })
          .catch(() => undefined)
      }
    },
    async point(pageId: string, frameId: string | undefined, x: number, y: number) {
      const root = roots.get(pageId)
      if (!root) throw new Error('Browser page session is not prepared')
      if (!frameId) return { x, y, sessionId: root }
      const routed = await route(pageId, frameId)
      const original = frames.get(frameId)
      if (original?.pageId !== pageId) throw new Error('Unknown or detached browser frame')
      let frame = original
      let originalOwner: Record<string, UntrustedInput> | undefined
      const visited = new Set<string>()
      while (frame.parentId) {
        if (visited.has(frame.id)) throw new Error('Invalid browser frame hierarchy')
        visited.add(frame.id)
        const parent = frames.get(frame.parentId)
        if (!parent) throw new Error('Browser parent frame detached')
        const owner = await wire.request(
          'DOM.getFrameOwner',
          { frameId: frame.id },
          { sessionId: parent.sessionId },
        )
        if (frame.id === frameId) originalOwner = owner
        await wire.request(
          'DOM.scrollIntoViewIfNeeded',
          { backendNodeId: owner.backendNodeId },
          { sessionId: parent.sessionId },
        )
        frame = parent
      }
      // Dispatch to the frame's renderer. Routing through the top-level compositor
      // immediately after scrolling an OOPIF can split mouse down/up across targets.
      // Same-renderer child coordinates still need their local-root transformation.
      if (sessions.get(routed.sessionId)?.frameId === frameId)
        return { x, y, sessionId: routed.sessionId }
      if (!originalOwner) throw new Error('Browser frame owner is unavailable')
      const model = await wire.request(
        'DOM.getBoxModel',
        { backendNodeId: originalOwner.backendNodeId },
        { sessionId: routed.sessionId },
      )
      const content = protocolRecord(model.model, 'frame box model').content
      if (!Array.isArray(content) || content.length !== 8)
        throw new Error('Invalid browser frame geometry')
      const quad = (content as UntrustedInput[]).map((value) =>
        protocolNumber(value, 'frame quad coordinate'),
      )
      const [x0 = 0, y0 = 0, x1 = 0, y1 = 0, , , x3 = 0, y3 = 0] = quad
      const current = await route(pageId, frameId)
      const dimensions = await wire.request(
        'Runtime.evaluate',
        {
          expression: '({width:innerWidth,height:innerHeight})',
          returnByValue: true,
          ...(current.contextId !== undefined ? { contextId: current.contextId } : {}),
        },
        { sessionId: current.sessionId },
      )
      const viewport = protocolRecord(
        protocolRecord(dimensions.result, 'frame viewport result').value,
        'frame viewport',
      )
      const width = protocolNumber(viewport.width, 'frame viewport width')
      const height = protocolNumber(viewport.height, 'frame viewport height')
      if (width <= 0 || height <= 0) throw new Error('Invalid browser frame viewport')
      return {
        x: x0 + ((x1 - x0) * x) / width + ((x3 - x0) * y) / height,
        y: y0 + ((y1 - y0) * x) / width + ((y3 - y0) * y) / height,
        sessionId: current.sessionId,
      }
    },
    closePage(pageId: string) {
      for (const [sessionId, owner] of sessions)
        if (owner.pageId === pageId) removeSession(sessionId)
      for (const frame of frames.values()) if (frame.pageId === pageId) frames.delete(frame.id)
      roots.delete(pageId)
    },
    dispose() {
      for (const remove of removers.splice(0)) remove()
      frames.clear()
      for (const id of changes.keys()) changed(id)
      sessions.clear()
      roots.clear()
    },
  }
}
