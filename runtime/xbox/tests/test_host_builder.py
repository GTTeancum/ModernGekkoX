#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Synthetic dependency-cache tests; no generated commercial-game source."""
import importlib.util
import json
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest
MODULE=Path(__file__).resolve().parents[1]/"tools/build_host_probe.py"
spec=importlib.util.spec_from_file_location("host_builder",MODULE)
build=importlib.util.module_from_spec(spec);spec.loader.exec_module(build)

class CacheTests(unittest.TestCase):
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory()
        self.root=Path(self.temp.name)/"path with spaces";self.root.mkdir()
        self.source=self.root/"sample.c";self.header=self.root/"value.h"
        self.source.write_text('#include "value.h"\nint answer(void){return VALUE;}\n')
        self.header.write_text("#define VALUE 7\n")
        self.obj=self.root/"sample.o";self.cc=str(Path(shutil.which("clang")).resolve())
        self.identity={"sha256":build.sha(self.cc)}
        self.command=[self.cc,"-std=c11","-O0"]
    def tearDown(self):
        self.temp.cleanup()
    def compile(self):
        return build.compile_cached(self.source,self.obj,self.command,self.identity,self.root)
    def test_identical_inputs_reuse(self):
        self.assertFalse(self.compile()["reused"]);self.assertTrue(self.compile()["reused"])
    def test_source_change_invalidates(self):
        self.compile();self.source.write_text("int answer(void){return 9;}\n")
        self.assertFalse(self.compile()["reused"])
    def test_header_change_invalidates(self):
        old=self.compile();self.header.write_text("#define VALUE 9\n");new=self.compile()
        self.assertFalse(new["reused"]);self.assertNotEqual(old["object_sha256"],new["object_sha256"])
    def test_object_corruption_invalidates(self):
        old=self.compile();self.obj.write_bytes(b"bad")
        new=self.compile();self.assertFalse(new["reused"]);self.assertEqual(old["object_sha256"],new["object_sha256"])
    def test_compiler_flags_invalidate(self):
        self.compile();self.command.append("-O1");self.assertFalse(self.compile()["reused"])
    def test_compiler_identity_invalidates(self):
        self.compile();self.identity={"sha256":"synthetic-new-toolchain"}
        self.assertFalse(self.compile()["reused"])
    def test_missing_header_fails_and_removes_cache(self):
        self.compile();self.header.unlink()
        with self.assertRaises(RuntimeError):self.compile()
        self.assertFalse(self.obj.exists());self.assertFalse(self.obj.with_suffix(".json").exists())
    def test_invalid_source_fails_and_removes_cache(self):
        self.compile();self.source.write_text("#error deliberate failure\n")
        with self.assertRaises(RuntimeError):self.compile()
        self.assertFalse(self.obj.exists());self.assertFalse(self.obj.with_suffix(".json").exists())
    def test_malformed_record_rebuilds(self):
        self.compile();self.obj.with_suffix(".json").write_text("{")
        self.assertFalse(self.compile()["reused"])
    def test_spaces_and_header_dependency_recorded(self):
        record=self.compile();self.assertIn(str(self.header),record["dependencies"])
        self.assertIn(str(self.source),record["dependencies"]);self.assertTrue(self.compile()["reused"])
    def test_empty_dependency_text_rejected(self):
        with self.assertRaises(ValueError):build.dependency_paths("target:",self.root)
if __name__=="__main__":unittest.main()
