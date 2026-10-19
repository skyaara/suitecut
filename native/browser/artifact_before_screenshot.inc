// SPDX-License-Identifier: MIT
// A real native scheduler pause, included inside Source.
void ArtifactBeforeScreenshot(int id, Dict config, SuiteCutArtifactController::Callback done) {
  const auto operation = operations_.Get(id); if (!operation) return;
  const auto generation = operation->generation;
  const auto client_token = config->GetString("clientToken").ToString();
  const auto token = action_pauses_.Pause(id, generation, client_token, [this, id, generation, config, done] {
    if (!operations_.IsCurrent(id,generation)) return;
    // Callback completion is canonical, even if a later physical capture must
    // restart in another document. Never invoke the user's callback twice.
    if (!artifact_configs_.contains(id) || !artifact_attempts_.contains(id)) return;
    artifact_configs_.at(id)->SetBool("beforeScreenshot",false);
    config->SetBool("beforeScreenshot",false);
    ArtifactWithContext(id,generation,artifact_attempts_.at(id),[this,id,generation,config,done](int context) {
      if (!operations_.IsCurrent(id,generation)) return;
      config->SetInt("__artifactContextId", context);
      operation_frames_[id] = main_frame_id_; operation_sessions_[id] = "";
      done(true, "");
    });
  });
  if (!token) { done(false, "Native screenshot pause capacity or identity is invalid"); return; }
  auto packet = Header("actionstage");packet->SetInt("id",id);
  packet->SetDouble("generation",static_cast<double>(generation));packet->SetDouble("token",static_cast<double>(*token));
  packet->SetString("clientToken",client_token);packet->SetString("stage","beforeScreenshot");SendPacket(packet);
}
