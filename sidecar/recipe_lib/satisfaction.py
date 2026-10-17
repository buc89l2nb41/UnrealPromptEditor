"""사용자 만족도 감지."""

from __future__ import annotations

import re
from typing import Tuple

# 긍정적 만족 마커 (저장 트리거)
SATISFACTION_MARKERS: tuple[str, ...] = (
    # 한글 - 강한 긍정
    "완벽",
    "최고",
    "훌륭",
    "대박",
    "저장해",
    "저장하자",
    "저장할게",
    "이걸로",
    "이대로",
    "확정",
    "끝",
    "딱이야",
    "마음에 들어",
    "맘에 들어",
    # 한글 - 일반 긍정
    "좋아",
    "좋네",
    "괜찮아",
    "괜찮네",
    "됐어",
    "됐다",
    "오케이",
    "ㅇㅋ",
    "굿",
    "나이스",
    # 영어
    "perfect",
    "excellent",
    "great",
    "awesome",
    "love it",
    "looks good",
    "looks great",
    "save this",
    "save it",
    "keep this",
    "done",
    "finish",
    "that's it",
    "good enough",
    "nice",
    "ok",
    "okay",
)

# 부정적 마커 (저장 안 함)
NEGATIVE_MARKERS: tuple[str, ...] = (
    # 한글
    "아니",
    "다시",
    "수정",
    "바꿔",
    "고쳐",
    "변경",
    "조정",
    "더",
    "덜",
    "좀",
    "근데",
    "그런데",
    "대신",
    "말고",
    "다르게",
    # 영어
    "no",
    "change",
    "fix",
    "redo",
    "again",
    "modify",
    "adjust",
    "but",
    "however",
    "instead",
    "more",
    "less",
    "different",
)

# 질문 마커 (판단 보류)
QUESTION_MARKERS: tuple[str, ...] = (
    "?",
    "뭐",
    "어떻게",
    "왜",
    "what",
    "how",
    "why",
    "can you",
    "could you",
)


def is_satisfied(message: str) -> Tuple[bool, str]:
    """사용자 메시지에서 만족 여부 감지.
    
    Args:
        message: 사용자 메시지
    
    Returns:
        (만족 여부, 감지된 마커 또는 이유)
    """
    if not message:
        return False, "empty"
    
    text = message.strip().lower()
    
    # 질문이면 판단 보류
    for marker in QUESTION_MARKERS:
        if marker in text:
            return False, "question"
    
    # 부정적 마커 체크 (긍정보다 우선)
    for marker in NEGATIVE_MARKERS:
        if marker.lower() in text:
            return False, f"negative:{marker}"
    
    # 긍정적 마커 체크
    for marker in SATISFACTION_MARKERS:
        if marker.lower() in text:
            return True, f"positive:{marker}"
    
    return False, "neutral"


def get_satisfaction_level(iterations: int, final_positive: bool) -> str:
    """만족도 레벨 결정.
    
    Args:
        iterations: 수정 횟수
        final_positive: 최종 긍정 응답 여부
    
    Returns:
        "low", "medium", "high"
    """
    if not final_positive:
        return "low"
    
    if iterations <= 1:
        return "high"  # 첫 번째 시도에 만족
    elif iterations <= 3:
        return "medium"  # 몇 번 수정 후 만족
    else:
        return "low"  # 많이 수정해야 했음


def should_ask_to_save(message: str, turn_count: int) -> bool:
    """저장 여부를 물어봐야 하는지 판단.
    
    Args:
        message: 사용자 메시지
        turn_count: 현재 대화 턴 수
    
    Returns:
        저장 물어봐야 하면 True
    """
    satisfied, reason = is_satisfied(message)
    
    if not satisfied:
        return False
    
    # 최소 2턴 이상 대화 후에만 저장 제안
    if turn_count < 2:
        return False
    
    # 강한 긍정 표현이면 바로 물어봄
    strong_positive = (
        "완벽", "최고", "저장", "perfect", "excellent", "save", "keep"
    )
    for marker in strong_positive:
        if marker in message.lower():
            return True
    
    return satisfied
