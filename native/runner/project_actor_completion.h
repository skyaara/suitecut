// SPDX-License-Identifier: MIT
#pragma once
#include "project_pool.h"
namespace suitecut::runner {
enum class ProjectActorCompletion {Rejected,Completed,CanceledBeforeStart};
// The caller has already retired raw callbacks, fixtures, files, output actors
// and descendants. Cancellation remains distinct from an actual worker exit.
inline ProjectActorCompletion FinishUnplannedProject(ProjectPool& pool,
    const ProjectPool::Admission& admission,ProjectPool::Outcome outcome){
  if(pool.CanceledBeforeStart(admission))return ProjectActorCompletion::CanceledBeforeStart;
  if(!pool.StartedWorkerExited(admission)||!pool.Complete(admission.generation,outcome))return ProjectActorCompletion::Rejected;
  return ProjectActorCompletion::Completed;
}
}
