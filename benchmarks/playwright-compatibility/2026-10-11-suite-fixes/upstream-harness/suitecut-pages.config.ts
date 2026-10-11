import config from './suitecut-core.config';
export default {...config,testMatch:['page/**/*.spec.ts'],timeout:10000,globalTimeout:600000,maxFailures:30};
