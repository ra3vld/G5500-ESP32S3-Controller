import configparser
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest

spec=importlib.util.spec_from_file_location('measure_features',Path(__file__).resolve().parents[1]/'tools/measure_features.py')
measure=importlib.util.module_from_spec(spec);spec.loader.exec_module(measure)


class MeasurementTests(unittest.TestCase):
    def test_pinned_dependencies_and_disabled_display(self):
        config=configparser.ConfigParser(interpolation=None)
        config.read_string('[env:base]\nlib_deps = arduino-libraries/LiquidCrystal @ 1.0.7\n'
                           '[env:child]\nextends = env:base\n')
        with tempfile.TemporaryDirectory() as directory:
            pool=Path(directory);library=pool/'LiquidCrystal';library.mkdir()
            (library/'library.properties').write_text('name=LiquidCrystal\nversion=1.0.7\n')
            self.assertEqual(measure.offline_libraries(config,'child',pool,{}),[library])
            self.assertEqual(measure.offline_libraries(config,'child',pool,{'DISPLAY_SUPPORT':0}),[])
            config['env:child']['lib_deps']='missing/library @ 1.0.0'
            with self.assertRaisesRegex(ValueError,'missing'):measure.offline_libraries(config,'child',pool,{})

    def test_combined_report_uses_paired_baseline(self):
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory)
            (root/'manifest.json').write_text(json.dumps({'revision':'abc','sourceSha256':'123'}))
            base=dict(measurementVersion=8,profile='board',flag='baseline',ok=True,binaryBytes=1000,dramBytes=200,iramBytes=50,appSlotBytes=1024)
            off=dict(base,flag='MQTT_SUPPORT',binaryBytes=800,dramBytes=204,iramBytes=40)
            for item in (base,off):(root/(item['flag']+'.json')).write_text(json.dumps(item))
            (root/'symbol-checks.json').write_text('[]')
            results=measure.summarize(root);row=next(r for r in results if r['flag']=='MQTT_SUPPORT')
            self.assertEqual(row['saved'],dict(binaryBytes=200,dramBytes=-4,iramBytes=10))
            self.assertIn('200 / -4 / 10',(root/'results.md').read_text())


if __name__=='__main__':unittest.main()
