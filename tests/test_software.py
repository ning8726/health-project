#!/usr/bin/env python3
import copy
import importlib.util
import json
import os
from pathlib import Path
import subprocess
import sys
import threading
import time
import unittest

BASE=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(BASE/'yun'))
import collector
spec=importlib.util.spec_from_file_location('checker',BASE/'mac/check_health.py')
checker=importlib.util.module_from_spec(spec);spec.loader.exec_module(checker)

class SoftwareTests(unittest.TestCase):
    def packet(self):
        return checker.demo_packet(1,1.0)

    def test_protocol_and_strict_numbers(self):
        p=self.packet();self.assertTrue(collector.validate(p))
        for address in (0x38,0x53,0x1d):
            q=copy.deepcopy(p);q['motion']['address']=address
            self.assertTrue(collector.validate(q))
        q=copy.deepcopy(p);q['motion']['address']=0x70
        self.assertFalse(collector.validate(q))
        for edit in [('protocol',1),('state',True),('reps',1.5),('goal',0),('bpm',float('nan'))]:
            q=copy.deepcopy(p);q[edit[0]]=edit[1];self.assertFalse(collector.validate(q))
        with self.assertRaises(ValueError):collector.loads('{"a":NaN}')
        q=copy.deepcopy(p);q['motion']['fresh']=False
        self.assertFalse(collector.validate(q))

    def test_stale_values_and_snapshot_isolation(self):
        state=collector.State();state.accept(json.dumps(self.packet()),0)
        self.assertTrue(state.snapshot(1)['link'])
        stale=state.snapshot(4.1)
        self.assertFalse(stale['link']);self.assertIsNone(stale['bpm'])
        self.assertIsNone(stale['motion']['x_g']);self.assertFalse(stale['motion']['fresh'])
        self.assertIsNotNone(state.latest['motion']['x_g'])

    def test_frozen_wifi_file_cannot_keep_data_live(self):
        state=collector.State();reader=checker.RemoteReader(state,'http://example.invalid/')
        p=self.packet();p['link']=True
        self.assertTrue(reader.accept(p,0))
        for t in [1,2,3,4,5]:self.assertFalse(reader.accept(p,t))
        self.assertFalse(state.snapshot(5)['link'])
        p['seq']=2;p['uptime_ms']=6000;self.assertTrue(reader.accept(p,6))
        self.assertTrue(state.snapshot(6)['link'])

    def test_fragmented_and_oversize_serial(self):
        payload=json.dumps(self.packet()).encode()
        f=collector.Framer();lines=[]
        for b in payload+b'\r\n':lines.extend(f.feed(bytes([b])))
        self.assertEqual(lines,[payload])
        f=collector.Framer()
        self.assertEqual(f.feed(b'x'*(collector.MAX_LINE+20)+b'\n'+payload+b'\n'),[payload])
        self.assertEqual(f.rejected,1)

    @unittest.skipUnless(hasattr(os,'openpty'),'POSIX pseudo-terminal required')
    def test_serial_reconnect_and_invalid_line(self):
        master,slave=os.openpty();device=os.ttyname(slave)
        state=collector.State();stop=threading.Event()
        thread=threading.Thread(target=collector.run,kwargs={'device':device,'state':state,
            'should_run':lambda:not stop.is_set(),'publisher':lambda *_:None},daemon=True)
        thread.start()
        try:
            deadline=time.monotonic()+3
            while not state.port and time.monotonic()<deadline:time.sleep(.02)
            self.assertEqual(state.port,device)
            os.write(master,b'not json\n'+json.dumps(self.packet()).encode()+b'\n')
            while state.received<1 and time.monotonic()<deadline:time.sleep(.02)
            self.assertEqual(state.received,1);self.assertEqual(state.rejected,1)
            self.assertTrue(state.snapshot(collector.clock())['link'])
            os.close(master);master=None
            deadline=time.monotonic()+2
            while state.port and time.monotonic()<deadline:time.sleep(.02)
            self.assertFalse(state.snapshot(collector.clock())['link'])
        finally:
            stop.set();thread.join(3)
            if master is not None:os.close(master)
            os.close(slave)

    def test_actual_firmware_json_matches_collector(self):
        binary=os.environ.get('FIRMWARE_TEST_BINARY')
        if not binary:self.skipTest('Set FIRMWARE_TEST_BINARY after compiling test_firmware.cpp')
        result=subprocess.run([binary,'--telemetry'],check=True,capture_output=True,text=True)
        lines=[line for line in result.stdout.splitlines() if line.strip()]
        self.assertGreater(len(lines),0)
        for line in lines:
            p=collector.loads(line);self.assertTrue(collector.validate(p),p)
            self.assertIsNone(p['bpm']);self.assertFalse(p['motion']['fresh'])

if __name__=='__main__':unittest.main(verbosity=2)
