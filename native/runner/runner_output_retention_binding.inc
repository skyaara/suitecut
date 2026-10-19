// SPDX-License-Identifier: MIT
// Included inside the native Runner class. No SDK/authored value supplies these
// fences. Parallel actors share actual host/file/step/soft/Source leases.
  void FinalizeOutputs(){
    if(!options_.preserve_output)return;
    sr::OutputRetentionFence fence;
    fence.hosts=shared_->active_workers;fence.descendants=shared_->descendants.Active();
    fence.file_jobs=shared_->file_budget.physical;fence.steps=shared_->step_budget.physical;
    fence.raw_callbacks=shared_->soft_budget.physical;fence.sources=shared_->source_leases.Outstanding();
    const auto observe=[&](const Runner& actor){
      fence.hosts+=actor.host_?1:0;fence.raw_callbacks+=actor.soft_.IdleReady()?0:1;
      fence.steps+=actor.steps_.IdleReady()?0:1;
      fence.file_callbacks+=actor.attachments_.IdleReady()?0:1;
      fence.output_preparation+=actor.outputs_.Physical();fence.sources+=actor.SourcesPhysical()?1:0;
    };
    observe(*this);for(const auto* actor:shared_->emergency_actors)if(actor&&actor!=this)observe(*actor);
    // Validate the immutable original report before admitting any removal. A
    // malformed/unassociated output or unfinished report cannot delete a tree.
    (void)sr::report_detail::Validate(report_);
    bool failure_recorded=false;
    sr::FinalizeNativeOutputRetention(report_,"/proc/self/exe",*options_.preserve_output,fence,
      {&shared_->file_budget.physical,sr::RunnerAttachments::kPhysical},[&](const std::string& error){
        if(!failure_recorded){failure_recorded=true;Global(Quote(error));}
      });
  }
