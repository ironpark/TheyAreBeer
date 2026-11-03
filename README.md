# TheyAreBeer

![TheyAreBeer](docs/theyarebeer.png)

They Are Billions 최적화 패치 ~~데아-비루~~

치지직 스트리머 **[똘똘똘이](https://chzzk.naver.com/1906dd57f578c255feca54700bcccfc9)** 님이 배포한 **They Are Billions 비공식 최적화 패치 모드**를 수정,개선한 소스 코드 저장소입니다. 
원본 모드 이름은 **TAB 최적화 패치(TABPerf)** 입니다.

대량의 좀비가 등장하는 장면에서 발생하는 끊김과 느려짐을 줄이는 것을 목표로 합니다. 게임 엔진을 보완하는 C# 모드와 여러 CPU 코어를 활용하는 네이티브 물리 DLL로 구성됩니다.

이 저장소에는 패치 소스와 빌드 스크립트가 들어 있습니다. 게임 본체, TABModLoader, 빌드된 설치 패키지는 포함하지 않습니다. 상세한 원본 소스 안내는 [README-SOURCE.txt](README-SOURCE.txt)를 참고하세요.

## 주요 기능

| 영역 | 최적화 내용 |
| --- | --- |
| 물리 연산 | Chipmunk2D 기반 물리 엔진의 위치·속도 계산, 충돌 탐색, 접촉 처리 등을 다중 코어로 처리 |
| 작업 분배 | 논리·물리 작업을 작은 단위로 나눠 CPU 작업 부하를 분산 |
| 게임 시간 | 게임 옵션의 **빠른 타이머**가 켜져 있을 때 실제 경과 시간을 이용해 시간 갱신을 부드럽게 처리 |
| 물리 보간 | 물리 단계의 계산 시각에 맞춰 화면에 표시할 위치의 보간 기준을 보정 |
| 그리기 순서 | Z 순서 변경량에 따라 정렬 방식과 갱신 주기를 조절 |
| 좀비 탐색 | 거리 지도를 활용해 도달할 수 없는 대상에 대한 불필요한 적 탐색을 생략 |
| 전력망 | 연결 요소 정보를 활용해 전력 연결 여부를 계산 |
| 소리 | 같은 상태의 소리 장치를 반복해서 켜는 호출을 줄임 |
| 체력바 | 선택 옵션으로 좀비 체력바를 얇은 막대 형태로 단순화 |

성능 개선 정도는 CPU, 맵, 유닛 수와 게임 상황에 따라 달라집니다.

## 빌드 준비

- Windows x64 환경
- Visual Studio 2019/2022 또는 Visual Studio Build Tools
  - 물리 DLL: **C++를 사용한 데스크톱 개발** 워크로드 및 MSVC x64 도구
  - 엔진 모드: **MSBuild 및 Roslyn C# 컴파일러**, .NET Framework 4.7.2 이상
- [TABModLoader](https://codeberg.org/JKstring/TABModLoader)의 참조 파일. 아래 세팅 스크립트가 내려받습니다.
  - TABModLoader가 패치한 `TheyAreBillions.exe`
  - TABModLoader 저장소의 `Lib` 폴더에 있는 `DXVision.dll`, `0Harmony.dll`

`ModInfos.json`에 명시된 요구 로더 버전은 `1.1.0`입니다. 네이티브 빌드 스크립트는 경로에 공백이 있으면 중단하므로, 저장소를 공백이 없는 경로에 두세요.

## 빌드 방법

저장소 루트에서 PowerShell로 실행합니다.

### 0. 세팅 (게임 확인, 참조 파일, 모드로더 패치)

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\setup.ps1
```

세팅 스크립트는 다음을 순서대로 처리합니다.

1. **게임 설치 확인**: Steam 레지스트리와 `libraryfolders.vdf`, 앱 매니페스트(`appmanifest_644930.acf`)로 They Are Billions 설치 폴더를 찾습니다. 찾지 못하면 `-GameDir`로 지정하세요.
2. **참조 파일**: TABModLoader 최신 릴리스의 `ModLoaderUpdate.zip`에서 패치된 실행 파일을 받아 `refs\TheyAreBillions.exe`로 저장합니다. 로더 저장소의 최신 `Lib\v*` 폴더에서는 `DXVision.dll`과 `0Harmony.dll`을 받습니다. `refs\`는 git이 추적하지 않습니다.
3. **모드로더 패치**: 게임의 `TheyAreBillions.exe`가 로더로 패치됐는지 확인하고, 아니면 패치합니다.
   - 원본은 같은 폴더에 `TheyAreBillions.exe.orig`로 보관합니다. 되돌리려면 이 파일을 `TheyAreBillions.exe`로 되돌려 놓거나, Steam에서 "게임 파일 무결성 확인"을 실행하세요.
   - 다른 버전의 로더로 이미 패치돼 있으면 받은 버전으로 바꿉니다. 이때 보관해 둔 원본은 그대로 둡니다.
   - 게임이 실행 중이면 멈추고 안내합니다. 게임 폴더에 쓸 권한이 없으면 관리자 PowerShell에서 다시 실행하라고 안내합니다.
   - Steam이 게임을 업데이트하거나 무결성을 확인하면 패치가 풀립니다. 그때는 세팅을 다시 실행하세요.

옵션:

- `-NoPatch`: 게임 파일은 건드리지 않고, 패치 상태만 보여 주고 참조 파일만 받습니다.
- `-GameDir <폴더>`: 게임 설치 폴더를 직접 지정합니다.
- `-Version v1.1.6`: 특정 릴리스를 받습니다.
- `-RefDir <폴더>`: 참조 파일을 저장할 폴더를 지정합니다.
- `-Force`: 이미 있어도 다시 받습니다.

### 1. 물리 DLL

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\build-native.ps1
```

출력 파일:

- `native\out\DXPhysicsNative.dll`: 최적화 물리 DLL
- `native\out\compare.exe`: 원작 DLL과 비교하는 도구(비교 도구 빌드 성공 시)

### 2. 엔진 모드

기본으로 `refs\`의 참조 파일을 사용합니다. 다른 폴더에 둔 참조 파일을 쓰려면 `-RefDir`로 지정하세요.

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\build-mod.ps1
```

출력 파일:

- `mod\out\TABPerf.dll`
- `mod\out\ModInfos.json`

컴파일러를 자동으로 찾지 못하면 `-Csc` 인자로 Roslyn의 `csc.exe` 경로를 직접 지정할 수 있습니다.

## 빌드한 모드 적용

TABModLoader가 설치된 게임 폴더의 `Mods\TABPerf`에 빌드 결과물을 배치합니다.

```text
<게임 설치 폴더>/
└─ Mods/
   └─ TABPerf/
      ├─ ModInfos.json
      ├─ TABPerf.dll
      └─ DXPhysicsNative.dll    # 물리 최적화를 함께 사용할 때 추가
```

엔진 모드는 모드 폴더에 있는 물리 DLL을 먼저 로드하도록 구현되어 있습니다. 물리 DLL을 넣지 않으면 원작 물리를 사용합니다. 게임 실행 과정에서 이미 같은 이름의 DLL이 로드된 경우에는 모드 폴더의 대체본을 사용하지 않으므로, 실행 후 로그에서 적용 여부를 확인하세요.

`Mods\Billions\Billions.dll`이 있으면 같은 최적화를 수행하는 다른 모드가 있는 것으로 판단해 TABPerf는 적용을 건너뜁니다. TABPerf가 여러 폴더에서 중복 로드되는 경우에도 한 번만 적용합니다.

위 방식으로 배치한 모드를 해제하려면 게임을 종료한 뒤 `Mods\TABPerf` 폴더를 `Mods` 밖으로 옮기면 됩니다.

## 설정과 로그

설정과 로그는 `%APPDATA%\TABPerf`에 저장됩니다.

| 항목 | 설명 |
| --- | --- |
| `billions.log` | 모드 로드, 개별 최적화 적용, 물리 DLL 적용 여부 및 오류 기록 |
| `options.json` | 게임 그래픽 옵션의 **좀비 체력바 가볍게** 설정 저장. 기본값은 꺼짐 |
| `engine.json` | 선택적으로 물리 계산 반복 횟수 지정 |
| `dxphysics-crash.txt` | 네이티브 DLL 내부 오류 진단 기록. 저장 폴더가 이미 존재할 때 기록 |

물리 반복 횟수를 직접 지정하려면 `engine.json`에 다음과 같이 작성합니다.

```json
{
  "physicsIterations": 10
}
```

허용 범위는 `1`~`30`이며, 설정하지 않으면 원작의 반복 횟수(기본 10)를 사용합니다. 이 값은 물리 계산 반복 횟수를 바꾸는 고급 설정입니다.

네이티브 물리 작업 스레드 수는 자동으로 결정됩니다. 필요하면 게임 실행 환경의 `DX_PHYS_THREADS` 환경 변수로 지정할 수 있으며, `0`은 작업 스레드를 끕니다.

## 소스 구성

```text
mod/                   C# 엔진 최적화 모드와 모드 정보
native/
├─ chipmunk/            수정된 Chipmunk2D 7.0.0
├─ dxphysics/           게임용 물리 API 래퍼, 작업 스레드 풀, 오류 진단
└─ harness/             원작 DLL과의 비교 도구
scripts/
├─ setup.ps1            TABModLoader 참조 파일 받기(refs/)
├─ build-mod.ps1        TABPerf.dll 빌드
└─ build-native.ps1     DXPhysicsNative.dll 및 비교 도구 빌드
README-SOURCE.txt      원본 소스 설명 및 구현 상세
LICENSE.txt            패치 코드의 MIT 라이선스
```

물리 DLL을 수정할 때는 `native/dxphysics/dxdiag.c`의 `dx_text_pad`를 유지해야 합니다. TABModLoader가 DLL 시작 주소의 `+0x13B3` 위치에 5바이트를 덮어쓰기 때문에, 해당 공간에 실제 코드가 배치되면 게임이 종료될 수 있습니다.

비교 도구의 기본 사용법은 다음과 같습니다.

```powershell
.\native\out\compare.exe "C:\Original\DXPhysicsNative.dll" ".\native\out\DXPhysicsNative.dll"
```

추가 인자는 `[장면|all] [몸체 수] [단계 수]`입니다. 원본 소스 안내에 따르면 병렬 옵션 `3`, `5`, `6`, `8`을 끈 조건에서 원작 DLL과 위치·속도의 비트 단위 일치를 확인했습니다. 기본 병렬 설정 전체에 대한 동일성 보장을 뜻하지는 않습니다.

## 제작 및 라이선스

- 제작·배포: **똘똘똘이 (`ddolddolddol-e`)** — [치지직 채널](https://chzzk.naver.com/1906dd57f578c255feca54700bcccfc9)
- 패치 코드: [MIT License](LICENSE.txt), Copyright (c) 2026 ddolddolddol-e
- Chipmunk2D: 별도의 [MIT License](native/chipmunk/LICENSE.txt)

MIT 라이선스 조건에 따라 수정 및 재배포할 수 있으며, 저작권 고지와 라이선스 문구를 유지해야 합니다.

They Are Billions는 **Numantian Games**의 게임입니다. 이 패치는 비공식 모드이며 Numantian Games와 제휴하지 않습니다.
