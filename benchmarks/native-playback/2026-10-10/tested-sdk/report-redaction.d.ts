export interface SuiteCutReportRedaction {
    value: string;
    replacement: string;
}
/** Builds a deterministic path and text redactor for persisted reports. */
export declare function createSuiteCutReportRedactor(redactions: readonly SuiteCutReportRedaction[]): (text: string) => string;
//# sourceMappingURL=report-redaction.d.ts.map