import { type PresentationPage } from './browser-control.js'
import {
  type SuiteCutHighlightOptions,
  type SuiteCutPoint,
  type SuiteCutRect,
  type SuiteCutWordTiming,
} from './types.js'

interface PresentationHighlightInput {
  rect: SuiteCutRect
  options: SuiteCutHighlightOptions
  durationMs: number
}

interface PresentationCaptionInput {
  text: string
  durationMs: number
  words?: SuiteCutWordTiming[]
}

interface PresentationCursorInput {
  point: SuiteCutPoint
  durationMs: number
}

/** Installs SuiteCut's non-interactive recording layer in the current document. */
export function installSuiteCutPresentation(): void {
  const syncZoom = (host: HTMLElement): void => {
    const computedZoom = Number.parseFloat(getComputedStyle(document.documentElement).zoom)
    const rootZoom = Number.isFinite(computedZoom) && computedZoom > 0 ? computedZoom : 1
    host.style.setProperty('zoom', String(1 / rootZoom))
    host.style.setProperty('--suitecut-cursor-scale', String(rootZoom))
  }
  const existingHost = document.documentElement.querySelector<HTMLElement>(
    '[data-suitecut-presentation]',
  )
  if (existingHost !== null) {
    syncZoom(existingHost)
    return
  }

  const host = document.createElement('div')
  host.setAttribute('data-suitecut-presentation', '')
  host.style.cssText =
    'position:fixed;inset:0;z-index:2147483647;pointer-events:none;overflow:hidden;contain:strict;'
  const shadow = host.attachShadow({ mode: 'open' })

  const style = document.createElement('style')
  style.textContent = `
    :host { all: initial; }
    #layer { position: fixed; inset: 0; pointer-events: none; overflow: hidden; }
    #cursor {
      position: absolute;
      left: 0;
      top: 0;
      width: 16px;
      height: 20px;
      opacity: 0;
      transform: translate3d(0, 0, 0);
      transform-origin: 3px 3px;
      will-change: transform, opacity;
    }
    #cursor-art { transform: scale(var(--suitecut-cursor-scale, 1)); transform-origin: 0 0; }
    #cursor svg { position: absolute; overflow: visible; display: none; }
    #cursor[data-shape="default"] .cursor-arrow { display: block; left: -8.75px; top: -4.15625px; }
    #cursor[data-shape="pointer"] .cursor-hand { display: block; left: -10.02335px; top: -5.77432px; }
    #cursor[data-shape="text"] .cursor-text { display: block; left: -14.05469px; top: -14.875px; }
    .highlight {
      position: absolute;
      box-sizing: border-box;
      pointer-events: none;
      will-change: opacity, filter;
    }
    .highlight-label {
      position: absolute;
      left: -1px;
      bottom: calc(100% + 8px);
      max-width: 280px;
      padding: 5px 8px;
      border-radius: 6px;
      color: white;
      background: rgb(2 6 23 / 90%);
      font: 650 13px/1.25 ui-sans-serif, system-ui, sans-serif;
      white-space: nowrap;
      overflow: hidden;
      text-overflow: ellipsis;
    }
    .caption {
      position: absolute;
      left: 50%;
      bottom: 42px;
      max-width: min(920px, calc(100vw - 80px));
      padding: 13px 20px;
      border: 1px solid rgb(255 255 255 / 13%);
      border-radius: 13px;
      color: white;
      background: rgb(2 6 23);
      box-shadow: 0 10px 36px rgb(0 0 0 / 47%);
      font: 650 24px/1.35 ui-sans-serif, system-ui, sans-serif;
      text-align: center;
      transform: translateX(-50%);
    }
    .caption-word {
      border-radius: 5px;
      transition: color 80ms linear, background-color 80ms linear;
    }
    .caption-word-current {
      color: #fde047;
      background: rgb(250 204 21 / 18%);
    }
    .ripple {
      position: absolute;
      width: 30px;
      height: 30px;
      margin: -15px 0 0 -15px;
      border: 3px solid rgb(250 204 21 / 85%);
      border-radius: 999px;
      will-change: opacity, transform;
    }
  `
  const layer = document.createElement('div')
  layer.id = 'layer'
  const cursor = document.createElement('div')
  cursor.id = 'cursor'
  cursor.setAttribute('data-suitecut-cursor', '')
  cursor.dataset.shape = 'default'
  // macOS artwork by ful1e5/apple_cursor, GPL-3.0. See assets/cursors for source and license.
  // Upstream color substitutions and hotspots are scaled to a 28px canvas.
  cursor.innerHTML = `
    <div id="cursor-art">
    <svg class="cursor-arrow" width="28" height="28" aria-hidden="true" viewBox="0 0 256 256" fill="none" xmlns="http://www.w3.org/2000/svg"><g filter="url(#filter0_d_40_365)"><path fill-rule="evenodd" clip-rule="evenodd" d="M84.1001 48.5601V173.06L110.8 146.56L136.3 207.56L158.3 197.06L133.8 139.06H172.8L84.1001 48.5601Z" fill="#000000"/><path d="M88.0281 44.7102L78.6001 35.0909V48.5601V173.06V186.268L87.9746 176.964L108.876 156.218L131.225 209.681L133.454 215.013L138.669 212.524L160.669 202.024L165.411 199.76L163.366 194.92L142.094 144.56H172.8H185.892L176.728 135.21L88.0281 44.7102Z" stroke="#FFFFFF" stroke-width="11"/></g><defs><filter id="filter0_d_40_365" x="55.1001" y="13.6218" width="155.883" height="230.843" filterUnits="userSpaceOnUse" color-interpolation-filters="sRGB"><feFlood flood-opacity="0" result="BackgroundImageFix"/><feColorMatrix in="SourceAlpha" type="matrix" values="0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 127 0" result="hardAlpha"/><feOffset dx="-3" dy="7"/><feGaussianBlur stdDeviation="7.5"/><feColorMatrix type="matrix" values="0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0.3 0"/><feBlend mode="normal" in2="BackgroundImageFix" result="effect1_dropShadow_40_365"/><feBlend mode="normal" in="SourceGraphic" in2="effect1_dropShadow_40_365" result="shape"/></filter></defs></svg>
    <svg class="cursor-hand" width="28" height="28" aria-hidden="true" viewBox="0 0 257 257" fill="none" xmlns="http://www.w3.org/2000/svg"><g filter="url(#filter0_d_40_341)"><path d="M81.0183 180.026C77.6138 175.993 73.4782 167.747 66.1179 157.738C61.9463 152.076 51.6011 141.415 48.5203 136C45.8471 131.214 46.1348 129.069 46.7702 125.103C47.897 118.048 55.6169 112.555 63.8523 113.296C70.0737 113.847 75.3482 117.7 80.0953 121.34C86.5499 126.275 89.6466 134.286 93.087 141.647C93.8232 143.222 94.2708 143.773 94 142C93.1537 136.459 83.6081 86.5647 83.5009 86.0046L83.495 85.9739L78.7682 61.3943C77.1058 52.7503 80.7622 42.5647 89.5 41.4999C98.1629 40.4443 106.163 46.1267 108.449 54.549L113.464 73.0246C113.821 74.3406 114.109 75.6552 114.333 77.0004C115.57 84.443 119.843 110.194 119.996 111.929C119.97 110.998 119.827 103.622 119.701 97.0462C119.575 90.4682 123.421 85.5 130 85.5C146 85.5 152.62 94.02 152.62 98.5C152.62 92 161.371 87.4611 167 87.4611C175.32 87.4611 181.08 89.468 183 94.5C184.92 99.5319 185.899 113.5 186 114C186.298 115.47 186.93 106.506 191 103C197.838 97.1093 208.5 100.5 209.881 111C210.271 113.967 210.36 119.705 210.36 124.647C210.36 130.455 210.216 133.949 209.881 138.151C209.509 142.644 208.478 152.8 206.98 157.72C205.966 161.045 202.645 168.456 199.331 173.039C199.22 173.193 199.113 173.336 199 173.489C197.587 175.404 187.496 189.232 186.192 195.112C184.778 201.426 185.245 201.471 184.969 205.953C184.778 209.048 185.547 212.822 186.038 214.851C186.224 215.62 185.709 216.394 184.922 216.472C182.015 216.763 175.549 217.291 171.627 216.704C166.94 215.996 166.893 208.615 165.395 205.942C163.333 202.257 157.368 201.089 155.654 203.807C152.957 208.11 147.155 215.827 143.055 216.311C135.36 217.217 119.716 216.703 106.964 216.552C106.164 216.542 105.558 215.811 105.658 215.017C106.063 211.79 106.53 204.298 102.705 201.28C99.0493 198.37 94.7559 193.472 90.9918 190.372L81.0183 180.026Z" fill="white"/><path d="M152.62 98.5C152.62 94.02 146 85.5 130 85.5V85.5C123.421 85.5 119.575 90.4682 119.701 97.0462C119.84 104.317 120 112.566 120 112C120 111.143 115.593 84.5792 114.333 77.0004C114.109 75.6551 113.821 74.3406 113.464 73.0246L108.449 54.549C106.163 46.1267 98.1629 40.4443 89.5 41.4999V41.4999V41.4999C80.7622 42.5647 77.1058 52.7503 78.7682 61.3943L83.495 85.9739C83.4983 85.9912 83.4976 85.9873 83.5009 86.0046C83.6081 86.5647 93.1537 136.459 94 142C94.2708 143.773 93.8232 143.222 93.087 141.647C89.6466 134.286 86.5499 126.275 80.0953 121.34V121.34C75.3482 117.7 70.0737 113.847 63.8523 113.296C55.6169 112.555 47.897 118.048 46.7702 125.103C46.1348 129.069 45.8471 131.214 48.5203 136C51.6011 141.415 61.9463 152.076 66.1179 157.738C73.4782 167.747 77.6138 175.993 81.0183 180.026L90.9918 190.372C94.7559 193.472 99.0493 198.37 102.705 201.28C106.53 204.298 106.063 211.791 105.658 215.017C105.558 215.811 106.164 216.542 106.964 216.552C119.716 216.703 135.36 217.217 143.055 216.311C147.155 215.827 152.957 208.11 155.654 203.807C157.368 201.089 163.333 202.257 165.395 205.942C166.893 208.615 166.94 215.996 171.627 216.704C175.549 217.291 182.015 216.763 184.922 216.472C185.709 216.394 186.224 215.62 186.038 214.851C185.547 212.822 184.778 209.048 184.969 205.953C185.245 201.471 184.778 201.426 186.192 195.112C187.496 189.232 197.587 175.404 199 173.489C199.113 173.336 199.22 173.193 199.331 173.039C202.645 168.456 205.966 161.045 206.98 157.72C208.478 152.8 209.509 142.644 209.881 138.151C210.216 133.949 210.36 130.455 210.36 124.647C210.36 119.705 210.271 113.967 209.881 111C208.5 100.5 197.838 97.1093 191 103C186.93 106.506 186.298 115.47 186 114C185.899 113.5 184.92 99.5319 183 94.5C181.08 89.468 175.32 87.4611 167 87.4611C161.371 87.4611 152.62 92 152.62 98.5ZM152.62 98.5C152.62 103.575 152.754 107.841 152.893 111C153.172 117.382 152.62 110.414 152.62 104.026C152.62 101.923 152.62 99.9074 152.62 98.5Z" stroke="black" stroke-width="10"/></g><path d="M175 183L175 145" stroke="black" stroke-width="8.96" stroke-linecap="round"/><path d="M152 182.899V144.92" stroke="black" stroke-width="8.96" stroke-linecap="round"/><path d="M128.7 143.92V181.899" stroke="black" stroke-width="8.96" stroke-linecap="round"/><defs><filter id="filter0_d_40_341" x="18.3013" y="26.1326" width="212.419" height="223.995" filterUnits="userSpaceOnUse" color-interpolation-filters="sRGB"><feFlood flood-opacity="0" result="BackgroundImageFix"/><feColorMatrix in="SourceAlpha" type="matrix" values="0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 127 0" result="hardAlpha"/><feOffset dx="-3.84" dy="8.96"/><feGaussianBlur stdDeviation="9.6"/><feColorMatrix type="matrix" values="0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0.3 0"/><feBlend mode="normal" in2="BackgroundImageFix" result="effect1_dropShadow_40_341"/><feBlend mode="normal" in="SourceGraphic" in2="effect1_dropShadow_40_341" result="shape"/></filter></defs></svg>
    <svg class="cursor-text" width="28" height="28" aria-hidden="true" viewBox="0 0 257 256" fill="none" xmlns="http://www.w3.org/2000/svg"><g clip-path="url(#clip0_495_34)"><g filter="url(#filter0_d_495_34)"><path d="M88.2013 28.5296C96.4509 28.0189 104.687 29.6958 112.15 33.4009C118.828 36.7164 124.691 41.5584 129.32 47.5506C133.949 41.5584 139.812 36.7164 146.49 33.4009C153.953 29.6958 162.189 28.0189 170.439 28.5296L172.24 28.6411V45.0011L170.109 44.766C163.16 43.9992 156.159 45.7059 150.228 49.6263C144.379 53.4926 139.898 59.296 137.502 66.1408V126.95H149.318V143.202H137.502V190.71C139.934 197.52 144.426 203.284 150.268 207.125C156.195 211.021 163.178 212.72 170.112 211.967L172.24 211.735V228.101L170.429 228.204C162.201 228.67 153.996 226.988 146.547 223.312C139.87 220.017 133.99 215.214 129.32 209.268C124.65 215.214 118.77 220.017 112.093 223.312C104.644 226.988 96.4384 228.67 88.2113 228.204L86.3999 228.101V211.691L88.5661 211.971C102.871 213.821 116.722 205.114 121.847 190.711V143.326H110.031V127.074H121.847V66.0149C116.835 51.6153 102.874 42.7963 88.5812 44.7597L86.3999 45.0593V28.6411L88.2013 28.5296Z" fill="#000000"/><path d="M129.32 39.9081C124.993 35.4135 119.946 31.6894 114.373 28.9224C106.133 24.8316 97.0247 22.9738 87.8923 23.5392L86.0909 23.6507C83.4545 23.8139 81.3999 25.9997 81.3999 28.6411V45.0593C81.3999 46.5041 82.0248 47.8782 83.1137 48.8278C84.2026 49.7773 85.649 50.2094 87.0803 50.0128L89.2616 49.7132C100.803 48.1278 112.353 55.0652 116.847 66.8948V122.074H110.031C107.27 122.074 105.031 124.313 105.031 127.074V143.326C105.031 146.087 107.27 148.326 110.031 148.326H116.847V189.814C112.256 201.653 100.791 208.51 89.2072 207.012L88.6023 211.691L89.2071 207.012L87.041 206.732C85.615 206.548 84.1787 206.986 83.0988 207.936C82.0189 208.885 81.3999 210.253 81.3999 211.691V228.101C81.3999 230.753 83.4698 232.943 86.1171 233.093L87.9284 233.196C97.0255 233.711 106.09 231.85 114.305 227.796C119.893 225.039 124.962 221.33 129.32 216.852C133.678 221.33 138.747 225.039 144.335 227.796C152.55 231.85 161.614 233.711 170.711 233.196L172.523 233.093C175.17 232.943 177.24 230.753 177.24 228.101V211.735C177.24 210.315 176.636 208.962 175.579 208.014C174.522 207.065 173.111 206.611 171.7 206.765L169.572 206.996C163.809 207.622 157.986 206.215 153.015 202.947C148.33 199.867 144.636 195.283 142.502 189.809V148.202H149.318C152.079 148.202 154.318 145.963 154.318 143.202V126.95C154.318 124.189 152.079 121.95 149.318 121.95H142.502V67.0253C144.606 61.5173 148.293 56.8989 152.985 53.7975C157.956 50.5114 163.79 49.0991 169.561 49.7358L171.692 49.9709C173.105 50.1268 174.517 49.6739 175.576 48.7254C176.635 47.7769 177.24 46.4226 177.24 45.0011V28.6411C177.24 25.9997 175.185 23.8139 172.549 23.6507L170.747 23.5392C161.615 22.9738 152.507 24.8316 144.267 28.9224C138.694 31.6894 133.646 35.4135 129.32 39.9081Z" stroke="#FFFFFF" stroke-width="10" stroke-linejoin="round"/></g></g><defs><filter id="filter0_d_495_34" x="58.3999" y="10.4399" width="135.84" height="249.84" filterUnits="userSpaceOnUse" color-interpolation-filters="sRGB"><feFlood flood-opacity="0" result="BackgroundImageFix"/><feColorMatrix in="SourceAlpha" type="matrix" values="0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 127 0" result="hardAlpha"/><feOffset dx="-3" dy="7"/><feGaussianBlur stdDeviation="7.5"/><feColorMatrix type="matrix" values="0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0.3 0"/><feBlend mode="normal" in2="BackgroundImageFix" result="effect1_dropShadow_495_34"/><feBlend mode="normal" in="SourceGraphic" in2="effect1_dropShadow_495_34" result="shape"/></filter><clipPath id="clip0_495_34"><rect width="256" height="256" fill="white" transform="translate(0.939941)"/></clipPath></defs></svg>
    </div>
  `
  layer.append(cursor)
  shadow.append(style, layer)
  document.documentElement.append(host)
  syncZoom(host)

  // Hit-test the animated position, including while the page changes under a stationary cursor.
  const updateCursor = (): void => {
    if (!host.isConnected) return
    if (cursor.style.opacity === '1') {
      const bounds = cursor.getBoundingClientRect()
      let target = document.elementFromPoint(bounds.x, bounds.y)
      while (target?.shadowRoot !== null && target?.shadowRoot !== undefined) {
        const inner = target.shadowRoot.elementFromPoint(bounds.x, bounds.y)
        if (inner === null || inner === target) break
        target = inner
      }
      let disabled = false
      let interactive = false
      let ancestor = target
      while (ancestor !== null) {
        disabled ||= ancestor.matches(':disabled, [aria-disabled="true"], [inert]')
        interactive ||= ancestor.matches(
          'button, a[href], area[href], summary, select, input[type="button"], input[type="submit"], input[type="reset"], input[type="checkbox"], input[type="radio"], [role="button"], [role="link"]',
        )
        const root = ancestor.getRootNode()
        ancestor = ancestor.parentElement ?? (root instanceof ShadowRoot ? root.host : null)
      }
      const cssCursor = target === null ? 'default' : getComputedStyle(target).cursor
      const text =
        cssCursor === 'text' ||
        cssCursor === 'vertical-text' ||
        target?.matches(
          'textarea, input:not([type]), input[type="text"], input[type="email"], input[type="password"], input[type="search"], input[type="tel"], input[type="url"], input[type="number"], [contenteditable=""], [contenteditable="true"]',
        ) === true
      cursor.dataset.shape = disabled
        ? 'default'
        : cssCursor === 'pointer' || interactive
          ? 'pointer'
          : text
            ? 'text'
            : 'default'
    }
    requestAnimationFrame(updateCursor)
  }
  requestAnimationFrame(updateCursor)
}

/** Removes SuiteCut's recording layer from the current document. */
export function removeSuiteCutPresentation(): void {
  document.documentElement.querySelector('[data-suitecut-presentation]')?.remove()
}

/** Animates the injected cursor to a viewport point. */
export async function moveSuiteCutCursor(input: PresentationCursorInput): Promise<void> {
  const host = document.documentElement.querySelector('[data-suitecut-presentation]')
  const cursor = host?.shadowRoot?.querySelector<HTMLElement>('#cursor')
  if (cursor === null || cursor === undefined) {
    throw new Error('SuiteCut cursor is not installed')
  }

  const previousX = Number(cursor.dataset.x ?? input.point.x)
  const previousY = Number(cursor.dataset.y ?? input.point.y)
  const from = `translate3d(${previousX}px, ${previousY}px, 0)`
  const to = `translate3d(${input.point.x}px, ${input.point.y}px, 0)`
  cursor.style.opacity = '1'
  cursor.style.transform = to
  cursor.dataset.x = String(input.point.x)
  cursor.dataset.y = String(input.point.y)

  const animation = cursor.animate(
    [
      {
        opacity: previousX === input.point.x && previousY === input.point.y ? 0 : 1,
        transform: from,
      },
      { opacity: 1, transform: to },
    ],
    { duration: input.durationMs, easing: 'cubic-bezier(.22,.8,.22,1)', fill: 'both' },
  )
  await animation.finished
  animation.cancel()
  cursor.style.opacity = '1'
  cursor.style.transform = to
}

/** Draws a click ripple at the injected cursor's current position. */
export async function pulseSuiteCutCursor(durationMs: number): Promise<void> {
  const host = document.documentElement.querySelector('[data-suitecut-presentation]')
  const shadow = host?.shadowRoot
  const layer = shadow?.querySelector<HTMLElement>('#layer')
  const cursor = shadow?.querySelector<HTMLElement>('#cursor')
  if (layer === null || layer === undefined || cursor === null || cursor === undefined) {
    throw new Error('SuiteCut cursor is not installed')
  }

  const ripple = document.createElement('div')
  ripple.className = 'ripple'
  ripple.style.left = `${Number(cursor.dataset.x ?? 0)}px`
  ripple.style.top = `${Number(cursor.dataset.y ?? 0)}px`
  layer.append(ripple)
  const animation = ripple.animate(
    [
      { opacity: 0.95, transform: 'scale(.35)' },
      { opacity: 0, transform: 'scale(1.35)' },
    ],
    { duration: durationMs, easing: 'ease-out', fill: 'both' },
  )
  await animation.finished
  ripple.remove()
}

/** Shows a highlight inside the page so the screencast records its animation. */
export async function showSuiteCutHighlight(input: PresentationHighlightInput): Promise<void> {
  const host = document.documentElement.querySelector('[data-suitecut-presentation]')
  const layer = host?.shadowRoot?.querySelector<HTMLElement>('#layer')
  if (layer === null || layer === undefined) {
    throw new Error('SuiteCut highlight layer is not installed')
  }

  const padding = input.options.paddingPx ?? 8
  const highlight = document.createElement('div')
  highlight.className = 'highlight'
  highlight.setAttribute('data-suitecut-highlight', '')
  highlight.style.left = `${input.rect.x - padding}px`
  highlight.style.top = `${input.rect.y - padding}px`
  highlight.style.width = `${input.rect.width + padding * 2}px`
  highlight.style.height = `${input.rect.height + padding * 2}px`
  highlight.style.border = `${input.options.borderWidthPx ?? 4}px ${input.options.borderStyle ?? 'solid'} ${input.options.borderColor ?? '#7C3AED'}`
  highlight.style.borderRadius = `${input.options.borderRadiusPx ?? 10}px`
  if (input.options.mode === 'fill' || input.options.mode === 'spotlight') {
    const fill = input.options.fillColor ?? '#7C3AED'
    const fillOpacity = input.options.fillOpacity ?? 0.08
    highlight.style.background = `color-mix(in srgb, ${fill} ${fillOpacity * 100}%, transparent)`
  }
  if (input.options.mode === 'spotlight') {
    const backdrop = input.options.backdropColor ?? '#000000'
    const backdropOpacity = input.options.backdropOpacity ?? 0.45
    highlight.style.boxShadow = `0 0 0 9999px color-mix(in srgb, ${backdrop} ${backdropOpacity * 100}%, transparent)`
  }
  if (input.options.label !== undefined) {
    const label = document.createElement('span')
    label.className = 'highlight-label'
    label.setAttribute('data-suitecut-highlight-label', '')
    label.textContent = input.options.label
    if (input.rect.y - padding < 48) {
      label.style.top = 'calc(100% + 8px)'
      label.style.bottom = 'auto'
    }
    if (input.rect.x - padding + 280 > window.innerWidth) {
      label.style.right = '-1px'
      label.style.left = 'auto'
    }
    highlight.append(label)
  }
  layer.append(highlight)

  const enterType = input.options.enter?.type ?? 'fade'
  const exitType = input.options.exit?.type ?? 'fade'
  const enterDuration = enterType === 'none' ? 0 : (input.options.enter?.durationMs ?? 180)
  const exitDuration = exitType === 'none' ? 0 : (input.options.exit?.durationMs ?? 140)
  const stableDuration = Math.max(0, input.durationMs - enterDuration - exitDuration)
  const enterOpacity = enterType.includes('fade') ? 0 : 1
  const exitOpacity = exitType.includes('fade') ? 0 : 1
  const enterFilter = enterType.includes('scale') ? 'brightness(1.35)' : 'brightness(1)'
  const exitFilter = exitType.includes('scale') ? 'brightness(1.25)' : 'brightness(1)'
  const enter = highlight.animate(
    [
      { opacity: enterOpacity, filter: enterFilter },
      { opacity: 1, filter: 'brightness(1)' },
    ],
    {
      duration: enterDuration,
      easing: input.options.enter?.easing ?? 'ease-out',
      fill: 'both',
    },
  )
  await enter.finished
  await new Promise((resolve) => window.setTimeout(resolve, stableDuration))
  const exit = highlight.animate(
    [
      { opacity: 1, filter: 'brightness(1)' },
      { opacity: exitOpacity, filter: exitFilter },
    ],
    {
      duration: exitDuration,
      easing: input.options.exit?.easing ?? 'ease-in',
      fill: 'both',
    },
  )
  await exit.finished
  highlight.remove()
}

/** Shows a caption for an already measured narration clip. */
export async function showSuiteCutCaption(input: PresentationCaptionInput): Promise<void> {
  const host = document.documentElement.querySelector('[data-suitecut-presentation]')
  const layer = host?.shadowRoot?.querySelector<HTMLElement>('#layer')
  if (layer === null || layer === undefined) {
    throw new Error('SuiteCut caption layer is not installed')
  }

  const caption = document.createElement('div')
  caption.className = 'caption'
  caption.setAttribute('data-suitecut-caption', '')
  const wordElements: HTMLElement[] = []
  let textOffset = 0
  for (const word of input.words ?? []) {
    caption.append(document.createTextNode(input.text.slice(textOffset, word.startOffset)))
    const element = document.createElement('span')
    element.className = 'caption-word'
    element.textContent = word.text
    caption.append(element)
    wordElements.push(element)
    textOffset = word.endOffset
  }
  if (wordElements.length === 0) caption.textContent = input.text
  else caption.append(document.createTextNode(input.text.slice(textOffset)))
  layer.append(caption)
  const timers: number[] = []
  for (const [index, word] of (input.words ?? []).entries()) {
    const element = wordElements[index]
    if (element === undefined) continue
    timers.push(
      window.setTimeout(() => element.classList.add('caption-word-current'), word.startMs),
      window.setTimeout(() => element.classList.remove('caption-word-current'), word.endMs),
    )
  }
  try {
    await new Promise((resolve) => window.setTimeout(resolve, input.durationMs))
  } finally {
    for (const timer of timers) window.clearTimeout(timer)
    caption.remove()
  }
}

/** Waits for application animations without waiting for SuiteCut's own recording layer. */
export async function waitForSuiteCutPageAnimations(timeoutMs: number): Promise<void> {
  await new Promise((resolve) => requestAnimationFrame(() => requestAnimationFrame(resolve)))
  const host = document.documentElement.querySelector('[data-suitecut-presentation]')
  const animations = document.getAnimations().filter((animation) => {
    const target = animation.effect instanceof KeyframeEffect ? animation.effect.target : null
    return !(target instanceof Node && host?.shadowRoot?.contains(target))
  })
  if (animations.length === 0) return

  await Promise.race([
    Promise.allSettled(animations.map((animation) => animation.finished)),
    new Promise((resolve) => window.setTimeout(resolve, timeoutMs)),
  ])
}

async function ensurePresentation(page: PresentationPage): Promise<void> {
  await page.evaluate(installSuiteCutPresentation)
}

export async function movePresentationCursor(
  page: PresentationPage,
  point: SuiteCutPoint,
  durationMs: number,
): Promise<void> {
  await ensurePresentation(page)
  await page.evaluate(moveSuiteCutCursor, { point, durationMs })
}

export async function pulsePresentationCursor(
  page: PresentationPage,
  durationMs: number,
): Promise<void> {
  await ensurePresentation(page)
  await page.evaluate(pulseSuiteCutCursor, durationMs)
}

export async function showPresentationHighlight(
  page: PresentationPage,
  rect: SuiteCutRect,
  options: SuiteCutHighlightOptions,
  durationMs: number,
): Promise<void> {
  await ensurePresentation(page)
  await page.evaluate(showSuiteCutHighlight, { rect, options, durationMs })
}

export async function showPresentationCaption(
  page: PresentationPage,
  text: string,
  durationMs: number,
  words?: SuiteCutWordTiming[],
): Promise<void> {
  await ensurePresentation(page)
  await page.evaluate(showSuiteCutCaption, {
    text,
    durationMs,
    ...(words === undefined ? {} : { words }),
  })
}

export async function waitForPresentationAnimations(
  page: PresentationPage,
  timeoutMs: number,
): Promise<void> {
  try {
    await page.evaluate(waitForSuiteCutPageAnimations, timeoutMs)
  } catch (error) {
    const message = error instanceof Error ? error.message : String(error)
    if (
      message.includes('Execution context was destroyed') ||
      message.includes('Cannot find context with specified id')
    ) {
      await page.waitForLoadState('domcontentloaded')
      return
    }
    throw error
  }
}
