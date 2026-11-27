"""Check timeout classification with real Trio and a simulated LDN radio."""
import contextlib
import sys
from pathlib import Path
from types import SimpleNamespace
import unittest
from unittest.mock import patch, AsyncMock

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'iz2sciv-network/ldn'))
import trio
import session
from main import error_details


class Timeouts(unittest.TestCase):
    def run_session(self, connect, deliver=None):
        args = SimpleNamespace(keys='fixture', phy='phy0', ifname='test', host=None)
        info = SimpleNamespace(channel=1)
        with patch.object(session.connection, 'load_keys', return_value={}), \
             patch.object(session.connection, 'scan', AsyncMock(return_value={'host': info})), \
             patch.object(session.connection, 'build_connect_params', return_value=object()), \
             patch.object(session.ldn, 'connect', connect), \
             patch.object(session.lobby, 'run', deliver or AsyncMock(return_value=0)):
            return trio.run(session.run, args)

    def test_plain_and_nested_association_timeouts_are_explained(self):
        for error in [trio.TooSlowError(), ExceptionGroup('nursery', [ExceptionGroup('inner', [trio.TooSlowError()])])]:
            @contextlib.asynccontextmanager
            async def connect(param):
                raise error
                yield
            with self.assertRaises(Exception) as caught:
                self.run_session(connect)
            self.assertIn('LDN connection timed out', error_details(caught.exception))

    def test_unrelated_error_is_not_lost_from_mixed_group(self):
        @contextlib.asynccontextmanager
        async def connect(param):
            raise ExceptionGroup('nursery', [trio.TooSlowError(), OSError('radio removed')])
            yield
        with self.assertRaises(Exception) as caught:
            self.run_session(connect)
        self.assertIn('radio removed', error_details(caught.exception))
        self.assertIn('LDN connection timed out', error_details(caught.exception))

    def test_delivery_timeout_is_not_reported_as_no_commands_sent(self):
        @contextlib.asynccontextmanager
        async def connect(param):
            yield object()
        with self.assertRaises(Exception) as caught:
            self.run_session(connect, AsyncMock(side_effect=trio.TooSlowError()))
        self.assertNotIn('LDN connection timed out', error_details(caught.exception))

    def test_success_runs_delivery_once(self):
        @contextlib.asynccontextmanager
        async def connect(param):
            yield object()
        deliver = AsyncMock(return_value=0)
        self.assertEqual(self.run_session(connect, deliver), 0)
        deliver.assert_awaited_once()

if __name__ == '__main__':
    unittest.main()
