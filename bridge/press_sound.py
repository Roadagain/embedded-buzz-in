"""Play sounds for first-place presses and promoted answerers."""

import logging
from pathlib import Path
import re
import shutil
import subprocess


LOGGER = logging.getLogger("bridge")
PRESS = re.compile(r"PRESS child=([1-9]\d*) rank=([1-9]\d*)(?: elapsed_ms=\d+)?")
WRONG = re.compile(r"JUDGE result=wrong child=([1-9]\d*)")


class PressSound:
    """Play first-place presses and promotions without queuing sounds."""

    def __init__(self, filename):
        self.process = None
        self.retired = []
        self.ranking = {}
        self.filename = None
        self.player = None
        if filename is None:
            return
        path = Path(filename).expanduser().resolve()
        self.player = shutil.which("afplay")
        if not path.is_file() or self.player is None:
            LOGGER.warning("Press sound disabled: sound file or afplay not found: %s", path)
            return
        self.filename = str(path)

    def handle_line(self, line):
        # Reap interrupted playback without waiting in the serial receive loop.
        self.retired = [process for process in self.retired if process.poll() is None]
        if self.process is not None:
            status = self.process.poll()
            if status is not None:
                if status != 0:
                    LOGGER.warning("Press sound playback failed (exit status %s)", status)
                self.process = None
        promoted = False
        should_play = False
        if line in ("BOOT", "RESET") or line.startswith("JUDGE result=correct child="):
            self.ranking.clear()
        press = PRESS.fullmatch(line)
        wrong = WRONG.fullmatch(line)
        if press is not None:
            child, rank = map(int, press.groups())
            if rank == 1:
                self.ranking.clear()
                should_play = True
            self.ranking[rank] = child
        elif wrong is not None and self.ranking.get(1) == int(wrong.group(1)):
            self.ranking = {rank - 1: child for rank, child in self.ranking.items() if rank > 1}
            promoted = 1 in self.ranking
            should_play = promoted
        if self.filename is None or not should_play:
            return
        if self.process is not None:
            if not promoted:
                LOGGER.debug("Press sound skipped: playback already active")
                return
            # A promotion must sound even immediately after the first press.
            self.process.terminate()
            self.retired.append(self.process)
            self.process = None
        try:
            self.process = subprocess.Popen(
                [self.player, self.filename],
                stdin=subprocess.DEVNULL,
                stdout=subprocess.DEVNULL,
                stderr=subprocess.DEVNULL,
            )
            LOGGER.info("Playing press sound: %s", self.filename)
        except OSError as error:
            LOGGER.warning("Press sound could not start: %s", error)

    def close(self):
        processes = self.retired + ([self.process] if self.process is not None else [])
        for process in processes:
            if process.poll() is not None:
                continue
            process.terminate()
            try:
                process.wait(timeout=0.5)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait()
        self.process = None
        self.retired = []
