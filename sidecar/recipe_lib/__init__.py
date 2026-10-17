"""VFX Recipe Library - 학습하는 이펙트 레시피 시스템"""

from .recipe_store import (
    VFXRecipe,
    save_recipe,
    load_recipe,
    list_recipes,
    search_recipes,
    get_recipe_index,
    update_recipe_index,
)
from .tag_extractor import extract_tags, TAG_KEYWORDS
from .satisfaction import is_satisfied, should_ask_to_save, SATISFACTION_MARKERS
from .clarify import (
    detect_ambiguous_effect,
    get_clarification_options,
    format_clarification_prompt,
    get_option_recipe,
    EFFECT_OPTIONS,
)
from .layers import (
    VFXLayer,
    get_layer_template,
    list_complex_effects,
    format_layer_guide,
    COMPLEX_EFFECT_LAYERS,
)

__all__ = [
    # Recipe store
    "VFXRecipe",
    "save_recipe",
    "load_recipe",
    "list_recipes",
    "search_recipes",
    "get_recipe_index",
    "update_recipe_index",
    # Tags
    "extract_tags",
    "TAG_KEYWORDS",
    # Satisfaction
    "is_satisfied",
    "should_ask_to_save",
    "SATISFACTION_MARKERS",
    # Clarify
    "detect_ambiguous_effect",
    "get_clarification_options",
    "format_clarification_prompt",
    "get_option_recipe",
    "EFFECT_OPTIONS",
    # Layers
    "VFXLayer",
    "get_layer_template",
    "list_complex_effects",
    "format_layer_guide",
    "COMPLEX_EFFECT_LAYERS",
]
