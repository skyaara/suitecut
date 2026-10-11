const fs = require('node:fs');
const path = require('node:path');
module.exports = class AuditProgress {
  onBegin(config, suite) {
    this.out = process.env.COMPAT_DIRECTORY;
    fs.mkdirSync(this.out, { recursive: true });
    this.started = Date.now(); this.counts = {}; this.done = 0; this.total = suite.allTests().length;
    this.stream = fs.createWriteStream(path.join(this.out, 'cases-live.jsonl'));
    this.update();
  }
  update(last) {
    fs.writeFileSync(path.join(this.out, 'progress.json'), JSON.stringify({startedAt:new Date(this.started).toISOString(), elapsedSeconds:(Date.now()-this.started)/1000, total:this.total, completed:this.done, statuses:this.counts, last}, null, 2));
  }
  onTestBegin(test) { this.update({title:test.titlePath(),location:test.location, running:true}); }
  onTestEnd(test,result) {
    this.done++; this.counts[result.status]=(this.counts[result.status]||0)+1;
    const row={id:test.id,title:test.titlePath(),location:test.location,expectedStatus:test.expectedStatus,outcome:test.outcome(),executionStatus:result.status,durationMs:result.duration,workerIndex:result.workerIndex,retry:result.retry,annotations:test.annotations,errors:result.errors.map(e=>({message:e.message,stack:e.stack})),attachments:result.attachments.map(a=>({name:a.name,path:a.path,contentType:a.contentType}))};
    this.stream.write(JSON.stringify(row)+'\n'); this.update({title:test.titlePath(),executionStatus:result.status});
  }
  onError(error) {fs.appendFileSync(path.join(this.out,'runner-errors.jsonl'),JSON.stringify(error)+'\n');}
  async onEnd(result) {this.update({runStatus:result.status});await new Promise(r=>this.stream.end(r));}
};
