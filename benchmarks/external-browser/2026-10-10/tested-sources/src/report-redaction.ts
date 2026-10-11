export interface SuiteCutReportRedaction {
  value: string
  replacement: string
}

/** Builds a deterministic path and text redactor for persisted reports. */
export function createSuiteCutReportRedactor(
  redactions: readonly SuiteCutReportRedaction[],
): (text: string) => string {
  const normalized = redactions
    .flatMap(({ value, replacement }) => {
      if (value.length <= 1) return []
      const portable = value.replaceAll('\\', '/')
      return portable === value
        ? [{ value, replacement }]
        : [
            { value, replacement },
            { value: portable, replacement },
          ]
    })
    .sort((left, right) => right.value.length - left.value.length)

  return (text: string): string =>
    normalized.reduce(
      (redacted, { value, replacement }) => redacted.replaceAll(value, replacement),
      text,
    )
}
