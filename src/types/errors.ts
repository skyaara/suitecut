import { type FilePath } from './primitives.js'

export interface SuiteCutSourceLocation {
  file: FilePath
  line: number
  column: number
}

export interface SuiteCutError {
  message: string
  name?: string
  stack?: string
  location?: SuiteCutSourceLocation
}
