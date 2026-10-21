// SPDX-License-Identifier: MIT
// Native physical capture lease, included inside Source. Layout metrics stay unchanged.
  int artifact_view_width_ = 0, artifact_view_height_ = 0, artifact_view_owner_ = 0;
  uint64_t artifact_view_generation_ = 0;
  SuiteCutArtifactController::Callback artifact_view_ready_;
  void ArtifactAcquireView(int id, int width, int height, SuiteCutArtifactController::Callback ready) {
    const auto operation = operations_.Get(id); if (!operation) return;
    if (!browser_ || artifact_view_owner_ || width <= 0 || height <= 0 ||
        width * scale_ > 32768 || height * scale_ > 32768 ||
        std::ceil(width * scale_) * std::ceil(height * scale_) > 64 * 1024 * 1024) {
      ready(false, "Native physical screenshot lease is unavailable or exceeds surface bounds"); return;
    }
    if(!ScreenshotAssertionOwns(id))writer_.PublicationAudit().ScreenshotAdmitted();
    artifact_view_owner_ = id; artifact_view_generation_ = operation->generation;
    artifact_view_width_ = width; artifact_view_height_ = height; artifact_view_ready_ = std::move(ready);
    browser_->GetHost()->WasResized(); browser_->GetHost()->Invalidate(PET_VIEW);
  }
  void ArtifactReleaseView(int id) {
    if (artifact_view_owner_ != id) return;
    artifact_view_ready_ = nullptr; artifact_view_owner_ = 0; artifact_view_generation_ = 0;
    artifact_view_width_ = artifact_view_height_ = 0;
    if (browser_ && !closing_page_) { browser_->GetHost()->WasResized(); browser_->GetHost()->Invalidate(PET_VIEW); }
  }
  bool ArtifactViewPaint(PaintElementType type, int width, int height) {
    if (closing_page_) return true;
    if (artifact_view_owner_) {
      if (type == PET_VIEW && artifact_view_ready_ &&
          std::abs(width - artifact_view_width_ * scale_) <= 1 && std::abs(height - artifact_view_height_ * scale_) <= 1) {
        auto ready = std::move(artifact_view_ready_); artifact_view_ready_ = nullptr;
        if (operations_.IsCurrent(artifact_view_owner_, artifact_view_generation_)) ready(true, "");
      }
      return true; // The media pipeline retains its actual original view.
    }
    if (type == PET_VIEW && (std::abs(width - width_ * scale_) > 1 || std::abs(height - height_ * scale_) > 1)) return true;
    return false;
  }
