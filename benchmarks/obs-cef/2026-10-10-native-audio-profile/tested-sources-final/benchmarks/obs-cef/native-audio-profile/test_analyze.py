import unittest
import numpy as np
from analyze import reference, sequences


class AudioIdentityTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.expected=reference(48000*4)

    def test_timestamp_jump_is_not_sample_loss(self):
        audio=self.expected[1000:1000+960*8]
        times=[i*20+(100 if i>=4 else 0) for i in range(8)]
        data,_=sequences(audio,self.expected,[960]*8,times)
        self.assertEqual(data['matchedWindows'],8)
        self.assertEqual(data['positiveJumpTotalMs'],0)
        self.assertEqual(data['negativeJumpTotalMs'],0)

    def test_120ms_sample_skip(self):
        audio=np.concatenate((self.expected[1000:1000+960*4],self.expected[1000+960*4+5760:1000+960*8+5760]))
        data,_=sequences(audio,self.expected,[960]*8)
        self.assertAlmostEqual(data['positiveJumpTotalMs'],120,places=2)

    def test_20ms_duplicate(self):
        audio=np.concatenate((self.expected[1000:4840],self.expected[3880:7720]))
        data,_=sequences(audio,self.expected,[960]*8)
        self.assertAlmostEqual(data['negativeJumpTotalMs'],20,places=2)

    def test_large_skip_reacquires_the_generated_sequence(self):
        audio=np.concatenate((self.expected[1000:4840],self.expected[4840+72000:4840+72000+960*12]))
        data,_=sequences(audio,self.expected,[960]*16)
        self.assertAlmostEqual(data['positiveJumpTotalMs'],1500,places=2)
        self.assertGreater(data['globalReacquisitions'],0)

    def test_inserted_silence_is_detected_without_missing_samples(self):
        audio=np.concatenate((self.expected[1000:4840],np.zeros(4800),self.expected[4840:8680]))
        data,_=sequences(audio,self.expected,[960]*13)
        self.assertEqual(data['silence']['interiorTotalMs'],100)
        self.assertAlmostEqual(data['negativeJumpTotalMs'],100,places=2)


if __name__=='__main__':unittest.main()
