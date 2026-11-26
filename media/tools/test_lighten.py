"""Filesystem regressions for lighten.py, without media encoder dependencies."""
import os
import unittest
from pathlib import Path
from tempfile import TemporaryDirectory

from lighten import outputs, publish


class LightenTests(unittest.TestCase):
    def test_output_is_hard_linked_to_another_source(self):
        with TemporaryDirectory() as temporary:
            base = Path(temporary)
            src, dst = base / "src", base / "dst"
            src.mkdir()
            dst.mkdir()
            (src / "a.png").write_bytes(b"original a")
            (src / "b.png").write_bytes(b"original b")
            os.link(src / "b.png", dst / "a.png")
            for path, out, _ in outputs(src, dst):
                publish(out, lambda candidate: candidate.write_bytes(path.read_bytes() + b" packed"))
            self.assertEqual((src / "a.png").read_bytes(), b"original a")
            self.assertEqual((src / "b.png").read_bytes(), b"original b")
            self.assertEqual((dst / "a.png").read_bytes(), b"original a packed")
            self.assertEqual((dst / "b.png").read_bytes(), b"original b packed")

    def test_two_formats_with_the_same_output_are_rejected_before_writing(self):
        with TemporaryDirectory() as temporary:
            base = Path(temporary)
            src, dst = base / "src", base / "dst"
            src.mkdir()
            (src / "song.wav").write_bytes(b"wave")
            (src / "song.flac").write_bytes(b"flac")
            with self.assertRaisesRegex(ValueError, "produce this file"):
                outputs(src, dst)
            self.assertFalse(dst.exists())

    def test_failed_preparation_keeps_the_previous_destination(self):
        with TemporaryDirectory() as temporary:
            out = Path(temporary) / "song.flac"
            out.write_bytes(b"existing")

            def reject(candidate):
                candidate.write_bytes(b"partial")
                raise ValueError("verification failed")

            with self.assertRaisesRegex(ValueError, "verification failed"):
                publish(out, reject)
            self.assertEqual(out.read_bytes(), b"existing")


if __name__ == "__main__":
    unittest.main()
