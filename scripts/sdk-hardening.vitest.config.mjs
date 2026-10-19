// Browser control SDK checks only; runner/assertion-framework checks have a separate scope.
export default {
  test: {
    include: ['tests/native-*.test.ts', 'tests/sdk-*.test.ts'],
    exclude: [
      '**/native-runner-*',
      '**/native-fixture-*',
      '**/native-step-*',
      '**/native-soft-*',
      '**/native-to-pass-*',
      '**/native-repeat-*',
      '**/native-assertion-*',
      '**/native-*expect*',
      '**/native-*attachment*',
      '**/native-callback-retries*',
      '**/native-callback-pipelines*',
      '**/native-callback-source-work*',
      '**/native-source-setup*',
      '**/native-screenshot-input*',
      '**/native-artifacts*',
    ],
    maxWorkers: 1,
  },
}
