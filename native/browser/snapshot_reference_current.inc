// SPDX-License-Identifier: MIT
// Shared by Source and the controlled coordinator host.
bool ReferenceCurrent(const std::shared_ptr<SnapshotReferenceCapture>& work){
  if(!operations_.IsCurrent(work->owner,work->generation))return false;
  if(operations_.IsExpired(work->owner,work->generation,Now())){Complete(work->owner,false,"Native operation timed out");return false;}
  const auto current=ReferenceDocument(work->document.frame);
  if(!current||*current!=work->document){Complete(work->owner,false,"Native snapshot reference document changed");return false;}
  return !work->all_current||work->all_current();
}
