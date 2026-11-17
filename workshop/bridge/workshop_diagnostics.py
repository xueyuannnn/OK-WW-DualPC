"""Local, bounded workshop traces; no network or input operations."""
import logging
from logging.handlers import RotatingFileHandler
from pathlib import Path


def configure_workshop_log(data):
    folder = Path(data) / 'logs'
    folder.mkdir(parents=True, exist_ok=True)
    handler = RotatingFileHandler(folder / 'workshop.log', maxBytes=5 * 1024 * 1024,
                                 backupCount=2, encoding='utf-8')
    handler.setFormatter(logging.Formatter('%(asctime)s %(levelname)s %(message)s'))
    # The SDK's headless logger may omit file logging. Attach after creating it.
    logger = logging.getLogger('ok')
    logger.addHandler(handler)
    logger.setLevel(logging.INFO)
    return logger


def bind_current_character(task, current):
    """A hidden HUD cannot erase the last valid character snapshot."""
    if type(current) is int and 0 <= current < len(task.chars) and task.chars[current] is not None:
        for char in task.chars:
            if char:
                char.is_current_char = char.index == current
    return next((char for char in task.chars if char and char.is_current_char), None)
