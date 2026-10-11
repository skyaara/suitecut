/** Supplies paths and optional attachment handling to a SuiteCut recording. */
export interface SuiteCutArtifactSink {
  pathFor(name: string): string
  attach(name: string, path: string, contentType: string): Promise<void>
}
