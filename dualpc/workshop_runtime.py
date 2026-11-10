"""Runtime-only compatibility for imported macros and external capture.

Original task/character classes and saved workshop source remain untouched.
"""
import ast

import cv2
import numpy as np


class ImportedCalls(ast.NodeTransformer):
    def visit_Call(self, node):
        node = self.generic_visit(node)
        if (isinstance(node.func, ast.Attribute) and node.func.attr == 'get_current_con'
                and not node.args and not node.keywords):
            return ast.copy_location(ast.Call(ast.Name('_dualpc_read_con', ast.Load()),
                                              [node.func.value], []), node)
        if (isinstance(node.func, ast.Name) and node.func.id == 'getattr'
                and len(node.args) >= 2 and isinstance(node.args[1], ast.Constant)
                and node.args[1].value == 'interaction'):
            return ast.copy_location(ast.Call(ast.Name('_dualpc_message_backend', ast.Load()),
                                              [node], []), node)
        return node

    def visit_Attribute(self, node):
        node = self.generic_visit(node)
        if node.attr == 'interaction' and isinstance(node.ctx, ast.Load):
            return ast.copy_location(ast.Call(ast.Name('_dualpc_message_backend', ast.Load()),
                                              [node], []), node)
        return node


def compile_import(code, filename):
    return compile(ast.fix_missing_locations(ImportedCalls().visit(ast.parse(code, filename))), filename, 'exec')


def shifted_con_fraction(crop, color_range):
    """Measure the colored arc at the established HUD ring location.

    Hue tolerates capture range/brightness changes. The annulus, contiguous
    arc and outer-background checks reject the central glyph and flat colors.
    """
    if crop is None or crop.ndim != 3 or min(crop.shape[:2]) < 28:
        return None
    height, width = crop.shape[:2]
    reference = np.uint8([[[sum(color_range[k]) // 2 for k in ('b', 'g', 'r')]]])
    hue = int(cv2.cvtColor(reference, cv2.COLOR_BGR2HSV)[0, 0, 0])
    hsv = cv2.cvtColor(crop, cv2.COLOR_BGR2HSV)
    angles = np.arange(180) * (2 * np.pi / 180)

    def sample(low, high):
        radii = np.linspace(low, high, 9) * height
        x = np.clip(np.rint(width / 2 + np.cos(angles)[:, None] * radii).astype(int), 0, width - 1)
        y = np.clip(np.rint(height / 2 + np.sin(angles)[:, None] * radii).astype(int), 0, height - 1)
        pixels = hsv[y, x]
        distance = np.abs(pixels[:, :, 0].astype(int) - hue)
        distance = np.minimum(distance, 180 - distance)
        return ((distance <= 11) & (pixels[:, :, 1] >= 60) & (pixels[:, :, 2] >= 145)).sum(axis=1) >= 3

    arc = sample(.35, .43)
    if sample(.46, .50).mean() > .50:
        return None
    # Chroma subsampling can make a one-bin hole; never bridge longer gaps.
    arc = arc | (np.roll(arc, 1) & np.roll(arc, -1))
    run = longest = 0
    for lit in np.tile(arc, 2):
        run = run + 1 if lit else 0
        longest = max(longest, run)
    fraction = min(1., longest / len(arc))
    if fraction < .08:
        return 0.
    return fraction


def read_con(owner):
    original = owner.get_current_con()
    task = getattr(owner, 'task', owner)
    if (not getattr(getattr(getattr(task, 'executor', None), 'interaction', None), 'is_remote', False)
            or not getattr(task, '_in_combat', False)):
        return original
    current = task.get_current_char()
    if owner is not task and owner is not current:
        return original
    if not task.in_team()[0]:
        return original
    from src.task.BaseCombatTask import con_colors
    index = current.ring_index
    if not isinstance(index, int) or not 0 <= index < len(con_colors):
        return original
    crop = task.get_con_box().crop_frame(task.frame)
    result = shifted_con_fraction(crop, con_colors[index])
    return original if result is None else max(original, result)


class MessageBackend:
    """A scoped PostMessage facade; real pointer-position state stays intact."""
    def __init__(self, interaction):
        self._interaction = interaction

    def __getattr__(self, name):
        return getattr(self._interaction, name)

    @property
    def mouse_pos(self):
        return self._interaction.mouse_pos or (0, 0)

    def post(self, message, wParam=0, lParam=0, hwnd=None):
        from .workshop_compat import NativeBridge, current_interaction, CompatibilityError
        interaction = current_interaction()
        if interaction is not self._interaction:
            raise CompatibilityError('Imported message backend is no longer active')
        return NativeBridge(lambda: interaction).message(0, message, wParam, lParam,
                                                        in_place=bool(interaction.client.buttons))


def message_backend(interaction):
    if isinstance(interaction, MessageBackend) or not getattr(interaction, 'is_remote', False):
        return interaction
    return MessageBackend(interaction)
