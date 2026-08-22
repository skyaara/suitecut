import { type Milliseconds, type SuiteCutPageId } from './primitives.js'

export type SuiteCutPageKind = 'main' | 'popup' | 'secondary'

export interface SuiteCutPage {
  id: SuiteCutPageId
  kind: SuiteCutPageKind
  openerPageId?: SuiteCutPageId
  initialUrl: string
  createdAtMs: Milliseconds
  closedAtMs?: Milliseconds
}
