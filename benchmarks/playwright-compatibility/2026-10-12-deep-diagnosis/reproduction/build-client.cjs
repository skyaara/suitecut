const root='/tmp/suitecut-playwright-upstream/playwright-1.62.1';
const esbuild=require('/workspace/suitecut/node_modules/.pnpm/esbuild@0.28.2/node_modules/esbuild');
const stub=name=>`${root}/packages/playwright-client/src/nodeStubs/${name}.ts`;
esbuild.build({absWorkingDir:root,bundle:true,format:'esm',platform:'browser',target:'es2020',entryPoints:[`${root}/packages/playwright-client/src/index.ts`],outfile:`${root}/packages/playwright-client/lib/index.mjs`,alias:Object.fromEntries(['fs','path','stream','util','inspector','async_hooks','events','crypto','colors/safe'].map(name=>[name,stub(name==='colors/safe'?'colors':name)])),inject:[stub('processShim')]}).catch(()=>process.exit(1));
