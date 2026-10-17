# Audio Oscilloscope 템플릿

> 오디오 입력에 반응하는 시각화 파티클

---

## 📋 기본 모듈 구성

| 스테이지 | 모듈 | 설명 |
|----------|------|------|
| System Spawn | **Audio Spectrum** | 오디오 분석 초기화 |
| Emitter Update | Emitter State | 루핑 |
| | Spawn Rate/Burst | 시각화 밀도 |
| Particle Spawn | Initialize Particle | 초기화 |
| | **Position from Audio** | 오디오 데이터로 위치 |
| Particle Update | **Scale by Audio** | 오디오로 크기 조절 |
| | **Color by Audio** | 오디오로 색상 조절 |
| Render | Sprite/Mesh Renderer | 렌더링 |

---

## 🎛️ 주요 파라미터

| 파라미터 | 기본값 | 설명 |
|----------|--------|------|
| **AudioSource** | None | 오디오 소스 액터 |
| **SpectrumBands** | 32-128 | 주파수 대역 수 |
| **FrequencyRange** | 20-20000 Hz | 분석 주파수 범위 |
| **Sensitivity** | 1.0 | 반응 민감도 |
| SmoothingFactor | 0.5 | 부드러움 정도 |
| SpawnCount | = Bands | 대역당 파티클 |

---

## 🔧 모듈 조합 효과 5가지

### 1. Audio + BarVisualization + ColorByFrequency → 이퀄라이저
```
추가 모듈: Grid Layout, Height by Amplitude
결과: 클래식 막대 이퀄라이저
용도: 음악 플레이어, DJ 시각화, 콘서트
파라미터:
  - Bands: 32
  - BarWidth: 10
  - MaxHeight: 200
  - Color: Low(Red) → Mid(Yellow) → High(Cyan)
```

### 2. Audio + CircularLayout + RadialPulse → 원형 파형
```
추가 모듈: Circular Position, Radial Scale
결과: 원형으로 펄스치는 시각화
용도: 스피커, 음성 AI, 라디오
파라미터:
  - Radius: 100
  - PulseScale: 1.0 + Amplitude × 2
  - Bands: 64
```

### 3. Audio + 3DWaveform + Ribbon → 파형 리본
```
추가 모듈: Ribbon Renderer, Waveform Position
결과: 오디오 파형을 따라가는 3D 리본
용도: 음악 게임, 비트 시각화
파라미터:
  - SampleRate: 60
  - WaveformWidth: 500
  - RibbonWidth: 5
```

### 4. Audio + ParticleBurst + BeatDetect → 비트 반응
```
추가 모듈: Beat Detection, Burst on Beat
결과: 비트에 맞춰 터지는 파티클
용도: 리듬 게임, 클럽 효과, 콘서트
파라미터:
  - BeatThreshold: 0.7
  - BurstCount: 50
  - BurstVelocity: 500
```

### 5. Audio + MeshScale + FrequencyFilter → 서브우퍼 진동
```
추가 모듈: Mesh Renderer, Low-Pass Filter
결과: 저음에 반응하는 큰 진동
용도: 스피커 진동, 베이스 반응
파라미터:
  - FrequencyRange: 20-200 Hz
  - MeshScale: 1.0 + Bass × 0.5
  - Mesh: SM_Speaker
```

---

## 🎨 활용 예시 5가지

### 1. 🎵 이퀄라이저 바 (Equalizer Bars)
```json
{
  "name": "FX_AudioEqualizer",
  "base": "AudioOscilloscope",
  "modules": {
    "modify": {
      "SpectrumBands": 32,
      "Sensitivity": 1.5
    },
    "add": ["GridLayout", "ScaleYByAmplitude", "ColorByBand"]
  },
  "layout": {
    "Type": "Line",
    "Spacing": 15,
    "BarWidth": 10
  },
  "color": {
    "Low": "Red",
    "Mid": "Yellow",
    "High": "Cyan"
  },
  "mesh": "SM_Cube"
}
```

### 2. 🔊 원형 스펙트럼 (Circular Spectrum)
```json
{
  "name": "FX_CircularSpectrum",
  "base": "AudioOscilloscope",
  "modules": {
    "modify": {
      "SpectrumBands": 64,
      "Sensitivity": 2.0
    },
    "add": ["CircularPosition", "RadialScale", "GlowMaterial"]
  },
  "layout": {
    "Type": "Circle",
    "Radius": 100,
    "InnerRadius": 50
  },
  "color": "Gradient (Angle-based)",
  "emissive": true
}
```

### 3. 💥 비트 버스트 (Beat Burst)
```json
{
  "name": "FX_BeatBurst",
  "base": "AudioOscilloscope",
  "modules": {
    "modify": {
      "FrequencyRange": "20-500"
    },
    "add": ["BeatDetection", "BurstOnBeat", "OmnidirectionalVelocity"]
  },
  "beat": {
    "Threshold": 0.8,
    "MinInterval": 0.2
  },
  "burst": {
    "Count": 30,
    "Velocity": 400,
    "Color": "Random Bright"
  }
}
```

### 4. 🌊 3D 파형 (3D Waveform)
```json
{
  "name": "FX_3DWaveform",
  "base": "AudioOscilloscope",
  "modules": {
    "modify": {
      "SampleCount": 128,
      "Sensitivity": 1.0
    },
    "add": ["WaveformPosition", "RibbonRenderer", "ColorByAmplitude"]
  },
  "waveform": {
    "Width": 400,
    "Height": 100,
    "Depth": "Time-based scroll"
  },
  "ribbon": {
    "Width": 3,
    "Color": "Green → Cyan"
  }
}
```

### 5. 🎤 음성 반응 아바타 (Voice Reactive Avatar)
```json
{
  "name": "FX_VoiceReactive",
  "base": "AudioOscilloscope",
  "modules": {
    "modify": {
      "FrequencyRange": "80-1000",
      "Sensitivity": 3.0
    },
    "add": ["MeshRenderer", "ScaleByVoice", "MaterialEmissive"]
  },
  "mesh": "SM_Orb",
  "scale": {
    "Base": 1.0,
    "VoiceMultiplier": 1.5
  },
  "emissive": {
    "Intensity": "0 → 10 (by amplitude)",
    "Color": "Cyan"
  }
}
```

---

## 📝 EditorPrompt 레시피

### 오디오 시각화 생성

```python
# 1. 시스템 생성
create_niagara_system(
  path="/Game/FX/NS_AudioViz",
  template="AudioOscilloscope"
)

# 2. 오디오 소스 연결
set_niagara_module_input(
  system="/Game/FX/NS_AudioViz",
  module="AudioSpectrum",
  input="AudioComponent",
  value="AudioSourceActor"
)

# 3. 스펙트럼 밴드 수 설정
set_niagara_module_input(
  system="/Game/FX/NS_AudioViz",
  module="AudioSpectrum",
  input="NumBands",
  value=64
)

# 4. 주파수 범위 설정
set_niagara_module_input(
  system="/Game/FX/NS_AudioViz",
  module="AudioSpectrum",
  input="FrequencyRange",
  value="(20, 15000)"
)

# 5. 크기 반응 추가
add_niagara_module(
  system="/Game/FX/NS_AudioViz",
  stage="ParticleUpdate",
  module="ScaleSpriteByAudio"
)
```

---

## 📐 오디오 분석 기초

### 주파수 대역

| 범위 | 이름 | 악기/소리 |
|------|------|-----------|
| 20-60 Hz | Sub Bass | 킥 드럼, 서브 베이스 |
| 60-250 Hz | Bass | 베이스 기타, 드럼 |
| 250-500 Hz | Low Mid | 보컬 저음 |
| 500-2000 Hz | Mid | 보컬, 기타 |
| 2000-4000 Hz | Upper Mid | 보컬 고음 |
| 4000-6000 Hz | Presence | 심벌, 하이햇 |
| 6000-20000 Hz | Brilliance | 공기감, 샤프 |

### FFT 해상도

```
Resolution = SampleRate / FFTSize

예: 44100 Hz / 2048 = 21.5 Hz per bin
→ 32 bands ≈ 690 Hz per band average
```

---

## 🔧 블루프린트 연동

### 오디오 소스 바인딩

```cpp
// C++에서 Niagara에 오디오 전달
void UpdateAudioData(UNiagaraComponent* Niagara, UAudioComponent* Audio)
{
    TArray<float> SpectrumData;
    Audio->GetCookedFFTData(SpectrumData);
    
    Niagara->SetNiagaraVariableFloatArray(
        "AudioSpectrum", 
        SpectrumData
    );
}
```

### 블루프린트 노드

```
1. Get Cooked FFT Data (Audio Component)
2. Set Niagara Variable (Float Array)
3. Tick에서 매 프레임 업데이트
```

---

## ✅ 사용 시점

| 상황 | 적합도 |
|------|--------|
| 음악 시각화 | ⭐⭐⭐⭐⭐ |
| 이퀄라이저 UI | ⭐⭐⭐⭐⭐ |
| 비트 반응 효과 | ⭐⭐⭐⭐ |
| 음성 반응 캐릭터 | ⭐⭐⭐⭐ |
| 일반 파티클 효과 | ⭐ (다른 템플릿 추천) |
| 오디오 없는 환경 | ❌ 해당 없음 |
