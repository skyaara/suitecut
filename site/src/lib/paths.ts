const basePath = import.meta.env.BASE_URL.replace(/\/$/u, '')

/** Builds a stable URL beneath Astro's configured deployment base. */
export function sitePath(path = '/'): string {
  const normalizedPath = path.startsWith('/') ? path : `/${path}`
  return `${basePath}${normalizedPath}`
}
