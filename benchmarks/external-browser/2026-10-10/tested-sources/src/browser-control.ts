import { type SuiteCutRect } from './types/geometry.js'

/** Browser operations shared by presentation and narration, independent of capture. */
export interface PresentationPage {
  evaluate<R>(fn: () => R | Promise<R>): Promise<R>
  evaluate<R, A>(fn: (arg: A) => R | Promise<R>, arg: A): Promise<R>
  waitForTimeout(ms: number): Promise<void>
  waitForLoadState(state: 'domcontentloaded'): Promise<void>
  isClosed(): boolean
}

/** Element operations needed by SuiteCut's authored presentation actions. */
export interface PresentationLocator<P extends PresentationPage = PresentationPage> {
  toString(): string
  page(): P
  boundingBox(): Promise<SuiteCutRect | null>
  evaluate<R, A>(
    fn: (element: SVGElement | HTMLElement, arg: A) => R | Promise<R>,
    arg: A,
  ): Promise<R>
  scrollIntoViewIfNeeded(): Promise<void>
  hover(): Promise<void>
  click(): Promise<void>
  fill(value: string): Promise<void>
  pressSequentially(value: string, options: { delay: number }): Promise<void>
}
