import importlib.util,pathlib,unittest
import numpy as np
path=pathlib.Path(__file__).with_name('analyze.py');spec=importlib.util.spec_from_file_location('publication_analyze',path);analyze=importlib.util.module_from_spec(spec);spec.loader.exec_module(analyze)
class Metrics(unittest.TestCase):
 def test_edges_are_not_interior_audio_loss(self):
  x=analyze.silence_summary([0,0,.1,.1,0,0,.1,0]);self.assertEqual(x['interiorTotalMs'],10);self.assertEqual(x['interiorMaximumMs'],10);self.assertEqual(x['leadingMs'],10);self.assertEqual(x['trailingMs'],5)
 def test_silent_source_does_not_pass_as_clean_audio(self):
  x=analyze.silence_summary([0,0,0]);self.assertEqual(x['activeBins'],0)
 def test_real_pts_hold_duration(self):
  x=analyze.motion([1,1,1,2,3],[0,.033,.066,.150,.183]);self.assertAlmostEqual(x['maxHoldMs'],150);self.assertAlmostEqual(x['maxPtsGapMs'],84);self.assertAlmostEqual(x['distinctFps'],2/.183)
 def test_logical_marker_jumps_and_pts_regressions(self):
  x=analyze.motion([1,2,5,4],[0,.03,.1,.09]);self.assertEqual(x['missedLogicalTicks'],2);self.assertEqual(x['markerRegressions'],1);self.assertEqual(x['nonIncreasingPts'],1)
 def test_known_audio_content_identifies_sample_offset(self):
  values=analyze.wave_reference(123456,4800);anchor=analyze.audio_anchor(values,123000);self.assertEqual(anchor['frame'],123456);self.assertGreater(anchor['correlation'],.999)
if __name__=='__main__':unittest.main()
