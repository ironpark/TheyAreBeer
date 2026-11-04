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
| 물리 엔진 교체 | 선택 설정으로 물리 엔진을 [Rapier](https://rapier.rs/) 2D로 교체 |
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
- Rapier 엔진(선택): [rustup](https://rustup.rs/), CMake 3.25 이상, git
- [TABModLoader](https://codeberg.org/JKstring/TABModLoader)의 참조 파일. 아래 세팅 스크립트가 내려받습니다.
  - TABModLoader가 패치한 `TheyAreBillions.exe`
  - TABModLoader 저장소의 `Lib` 폴더에 있는 `DXVision.dll`, `0Harmony.dll`

`ModInfos.json`에 명시된 요구 로더 버전은 `1.1.0`입니다. 네이티브 빌드 스크립트는 경로에 공백이 있으면 중단하므로, 저장소를 공백이 없는 경로에 두세요.

## 빌드와 설치

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

### 1. 빌드

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\build.ps1
```

빌드 스크립트 하나로 물리 엔진과 엔진 모드를 모두 빌드합니다. 일부만 빌드하려면 `-Target`에 대상을 쉼표로 나열하세요(예: `-Target chipmunk,mod`). 대상은 아래 순서로 빌드됩니다.

| 대상 | 출력 파일 | 설명 |
| --- | --- | --- |
| `chipmunk` | `native\out\DXPhysicsNative.dll` | Chipmunk2D를 여러 코어로 계산하는 물리 DLL |
| `harness` | `native\out\compare.exe` | 원작 DLL과 대체본을 함께 돌려 비교하는 도구 |
| `rapier` | `native\out\rapier\DXPhysicsNative.dll`, `rapier2d_ffi.dll`, `rapier2d_ffi_avx2.dll` | [Rapier](https://rapier.rs/) 2D 엔진으로 동작하는 물리 DLL |
| `mod` | `mod\out\TABPerf.dll`, `mod\out\ModInfos.json` | 엔진 모드 |

- `-Target`을 생략하면(`all`) 모두 빌드합니다. 이때 비교 도구 빌드가 실패하면 경고만 남기고, Rapier에 필요한 도구(git, cmake, rustup)가 없으면 Rapier를 건너뜁니다. 대상을 직접 지정하면 실패를 오류로 처리합니다.
- 엔진 모드는 기본으로 `refs\`의 참조 파일을 씁니다. 다른 폴더는 `-RefDir`로 지정하고, Roslyn 컴파일러를 자동으로 찾지 못하면 `-Csc`로 `csc.exe` 경로를 지정하세요.
- 각 단계의 컴파일 기록은 출력 폴더의 `build.log`, `build-harness.log`, `rapier-build*.log`에 남습니다. 실패하면 오류 줄을 보여 주고 멈춥니다.

Rapier 빌드는 Rapier의 공식 C 바인딩을 [공식 문서](https://rapier.rs/docs/user_guides/c/getting_started_c)의 CMake 빌드로 만들며, 다음 순서로 진행합니다.

1. Rapier 저장소를 고정 커밋으로 `native\out\rapier-src`에 받습니다.
2. 필요한 Rust 버전(1.99.1)이 없으면 설치합니다.
3. `-DRAPIER_DIMENSION=2 -DRAPIER_PRECISION=32 -DRAPIER_ENABLE_PARALLEL=ON -DRAPIER_SIMD_LANES=8`으로 두 번 빌드하고 CMake 통합 테스트를 돌립니다. 한 번은 모든 x64 CPU용, 한 번은 AVX2·FMA용(Rust `+avx2,+fma`)입니다. AVX2 빌드는 CPU와 OS가 AVX2를 지원할 때만 불러오며, 없어도 동작합니다.
4. 같은 정의(`RAPIER_DIM2`, `RAPIER_F32`, `RAPIER_PARALLEL`)로 `DXPhysicsNative.dll`을 빌드합니다. 이 DLL은 같은 폴더의 `rapier2d_ffi.dll`을 불러옵니다.

### 2. 설치

설치 스크립트가 빌드 결과물을 게임의 `Mods\TABPerf`에 넣습니다. 게임 파일은 바꾸지 않습니다. 게임을 끈 상태에서 실행하세요.

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\install.ps1 -Engine chipmunk
```

```text
<게임 설치 폴더>/
└─ Mods/
   └─ TABPerf/
      ├─ ModInfos.json
      ├─ TABPerf.dll
      ├─ chipmunk/
      │  └─ DXPhysicsNative.dll       # Chipmunk 다중 코어 (native\out)
      └─ rapier/                      # Rapier를 빌드했을 때 (native\out\rapier)
         ├─ DXPhysicsNative.dll
         ├─ rapier2d_ffi.dll
         └─ rapier2d_ffi_avx2.dll
```

- `-Engine chipmunk|rapier|original`은 `engine.json`의 `physicsEngine`을 함께 정합니다(아래 [물리 엔진 선택](#물리-엔진-선택)). 생략하면 설정을 바꾸지 않습니다.
- 빌드하지 않은 엔진은 건너뜁니다.
- 예전 설치 방식이 남긴 파일을 정리합니다. 모드 폴더 바로 아래의 물리 DLL을 지우고, 게임 폴더에 `DXPhysicsNative.dll.orig`가 있으면 대체본으로 바뀐 게임 폴더의 DLL을 그것으로 되돌립니다.
- 게임이 실행 중이면 멈춥니다. 게임 폴더에 쓸 권한이 없으면 관리자 PowerShell에서 다시 실행하라고 안내합니다.

배포용으로는 `Mods\TABPerf` 폴더를 그대로 묶으면 됩니다. 엔진 DLL은 하위 폴더에 두어야 합니다. TABModLoader는 모드 폴더 바로 아래의 `*.dll`을 모두 .NET 어셈블리로 열어 보므로, 물리 DLL이 그 자리에 있으면 로더 로그에 `加载DLL失败` 줄이 남습니다.

#### 물리 DLL 연결 방식

게임이 물리 DLL을 부르는 선언(`DXVision.DXPhysicsNative`의 `[DllImport("DXPhysicsNative.dll")]`)은 .NET Framework가 게임 폴더의 `DXPhysicsNative.dll`에 연결합니다. 그래서 같은 이름의 DLL을 모드 폴더에 두기만 하면, 미리 불러 두어도 게임은 게임 폴더의 원작 DLL을 씁니다. TABModLoader도 네이티브 DLL은 따로 처리하지 않습니다.

엔진 모드는 게임 파일을 바꾸지 않고, 게임 코드의 물리 호출을 고쳐 이 문제를 피합니다.

1. 고른 엔진의 DLL을 전체 경로로 불러옵니다. 게임 폴더 물리 DLL의 함수가 대체본에 모두 있는지 확인합니다. 게임이 선언만 하고 원작에도 없는 함수 4개는 확인에서 뺍니다.
2. 게임의 선언 47개와 같은 이름, 같은 시그니처, 같은 마샬링의 P/Invoke 메서드를 실행 중에 만듭니다. 이때 `DllImport`에는 대체본의 전체 경로를 씁니다.
3. 게임 어셈블리에서 이 선언을 부르는 메서드(지금 버전은 `DXPhysicBody`와 `DXPhysicWorld`의 36개)를 IL에서 찾습니다. Harmony 트랜스파일러로 호출 대상만 새 메서드로 바꿉니다.

호출은 여전히 일반 P/Invoke라서 호출 비용은 원작과 같습니다. 같은 조건(900%, 마지막 웨이브 4배, 60초)에서 Rapier를 게임 폴더에 넣은 경우와 비교했을 때, 좀비 수 구간별 단계 시간이 같거나 조금 짧았습니다(3만 마리대 35.2 ms 대 36.4 ms, 4만 마리대 48.1 ms 대 49.2 ms).

한 DLL에서 받은 핸들이 다른 DLL로 넘어가지 않도록, 연결은 전부 되거나 전혀 안 되거나 둘 중 하나입니다. 대체본에 함수가 빠졌거나 패치 하나가 실패하면 모두 되돌리고 게임 폴더의 DLL을 씁니다. 연결 결과는 `billions.log`의 `물리 연결:` 줄에 남습니다. 성능 시험 모드는 끝날 때 게임 폴더의 물리 DLL이 한 번도 불리지 않았는지 확인해 기록합니다.

`Mods\Billions\Billions.dll`이 있으면 같은 최적화를 수행하는 다른 모드가 있는 것으로 판단해 TABPerf는 적용을 건너뜁니다. TABPerf가 여러 폴더에서 중복 로드되는 경우에도 한 번만 적용합니다.

모드를 지우려면 `install.ps1 -Uninstall`을 실행합니다. `Mods\TABPerf`를 지웁니다. `%APPDATA%\TABPerf`의 설정과 로그는 남깁니다. 모드만 잠시 끄려면 게임을 끈 뒤 `Mods\TABPerf` 폴더를 `Mods` 밖으로 옮기면 됩니다.

## 설정과 로그

설정과 로그는 `%APPDATA%\TABPerf`에 저장됩니다.

| 항목 | 설명 |
| --- | --- |
| `billions.log` | 모드 로드, 개별 최적화 적용, 물리 DLL 적용 여부 및 오류 기록 |
| `options.json` | 게임 그래픽 옵션의 **좀비 체력바 가볍게** 설정 저장. 기본값은 꺼짐 |
| `engine.json` | 선택적으로 물리 엔진(`physicsEngine`), 물리 계산 반복 횟수(`physicsIterations`), 성능 시험 설정 지정 |
| `dxphysics-crash.txt` | 네이티브 DLL 내부 오류 진단 기록. 저장 폴더가 이미 존재할 때 기록 |

물리 반복 횟수를 직접 지정하려면 `engine.json`에 다음과 같이 작성합니다.

```json
{
  "physicsIterations": 10
}
```

허용 범위는 `1`~`30`이며, 설정하지 않으면 원작의 반복 횟수(기본 10)를 사용합니다. 이 값은 물리 계산 반복 횟수를 바꾸는 고급 설정입니다.

### 물리 엔진 선택

`engine.json`의 `physicsEngine`으로 물리 엔진을 고릅니다. `install.ps1 -Engine`으로도 정할 수 있습니다. 게임을 다시 시작해야 적용됩니다.

```json
{
  "physicsEngine": "rapier"
}
```

| 값 | 엔진 |
| --- | --- |
| `chipmunk` (기본) | 모드 폴더의 `chipmunk\DXPhysicsNative.dll`. 원작과 같은 Chipmunk2D를 여러 코어로 계산 |
| `rapier` | 모드 폴더의 `rapier\DXPhysicsNative.dll`. [Rapier](https://rapier.rs/) 2D 엔진 |
| `original` | 게임 폴더의 원작 DLL. 모드의 다른 최적화는 그대로 씀 |

고른 엔진을 쓸 수 없으면(파일이 없거나 연결 확인에 실패하면) 로그에 남기고 Chipmunk 판을 씁니다. Chipmunk 판도 쓸 수 없으면 원작 DLL을 씁니다. 어느 엔진이 쓰였는지는 `billions.log`의 `설치 끝 — 물리 DLL:` 줄에서 확인할 수 있습니다.

Rapier 엔진은 원작과 같은 함수로 동작하지만, 계산 방식이 달라 결과가 원작과 똑같지는 않습니다.

- 몸체가 밀리고 겹치는 모양이 원작과 조금 다릅니다.
- 충돌 시작(begin) 콜백이 `false`를 돌려준 쌍은, 원작처럼 같은 단계부터가 아니라 다음 단계부터 무시됩니다.
- 충돌 콜백은 단계 계산이 끝난 뒤 한꺼번에 호출됩니다.
- `Body_SetSize`는 실제로 크기를 바꿉니다. 원작 DLL에서는 움직이는 사각형 몸체의 크기 변경이 다음 단계에 사라집니다.
- 반지름이 있는 질의(`radius > 0`)는 사각형 모서리를 둥글리지 않고 계산합니다.

작업 스레드 수는 Chipmunk 판과 같이 `DX_PHYS_THREADS`로 지정합니다. Rapier 솔버 반복 횟수는 기본으로 게임이 넘기는 값(10, `physicsIterations`로 바꾼 경우 그 값)을 쓰고, `DX_RAPIER_ITERATIONS` 환경 변수(1~30)로 따로 지정할 수 있습니다. 반복을 줄이면 몸체가 더 겹쳐 접촉 계산이 늘어나기 때문에, 오히려 느려질 수 있습니다.

Rapier 판은 Rapier의 기능을 그대로 씁니다. 충돌 필터는 충돌 그룹(OR 판정)으로, 충돌 시작·분리 콜백은 충돌 이벤트로 처리합니다. 멈춘 몸체는 잠자기(sleep)에 들어가 계산에서 빠집니다. 잠자기는 `DX_RAPIER_SLEEP=0`으로 끌 수 있습니다. AVX2 빌드는 `DX_RAPIER_AVX2=0`으로 끄고 기본 빌드를 쓸 수 있습니다. 로그의 물리 DLL 줄에 `Rapier 대체본 (AVX2)`로 표시됩니다. 디버그 빌드 Rapier 라이브러리(약 100배 느림)를 불러오면 같은 줄에 경고가 붙습니다. 접촉 강성은 게임의 긴 단계(1/12초)에 맞춰 줄이며, `DX_RAPIER_SOFTNESS=0`으로 Rapier 기본값을 쓸 수 있습니다. `DX_RAPIER_CCD=0`은 연속 충돌 검사(CCD)를 꺼 단계를 줄이지만(몸체 6만 개 밀집에서 r2Step 약 17% 감소), 밀집 장면에서 벽을 통과하는 몸체가 늘어 기본으로는 켜 둡니다. 값이 NaN이 되어 Rapier가 격리한 몸체는 마지막 정상 위치에서 되살립니다.

네이티브 물리 작업 스레드 수는 자동으로 결정됩니다. 필요하면 게임 실행 환경의 `DX_PHYS_THREADS` 환경 변수로 지정할 수 있으며, `0`은 작업 스레드를 끕니다.

## 성능 측정

### 실제 게임 (900% 생존 게임, 마지막 웨이브 4배)

측정 환경: AMD Ryzen Threadripper PRO 7965WX(논리 프로세서 48개), Windows 11, They Are Billions Steam V1.0.14 (64비트), TABModLoader V1.1.6.

| 항목 | 설정 |
| --- | --- |
| 맵 | 생존 게임, 테마 「부식의 땅」, 감염체 최다(150%), 80일. 점수 배율 900%, 시드 고정(`20261104`)이라 엔진마다 같은 맵 |
| 부하 | 레벨 시작 5초 뒤 피해를 끄고(`CLife.DamageAvailable`, 게임의 디버그 스위치) 마지막 웨이브를 4번 발생. 생성기 4곳, 한 번에 생성기당 약 4,350마리 |
| 진행 | 피해가 없어 좀비가 죽지 않고 지휘본부도 무너지지 않으므로, 좀비가 계속 늘며 지휘본부로 몰립니다 |
| 기록 | 240초 동안 1초마다 프레임 수·프레임 시간, 물리 단계 수·단계 시간(`Space_Step` 한 번), 좀비 수 |

좀비가 생성되는 속도는 엔진이 게임을 얼마나 빨리 진행시키느냐에 따라 달라집니다. 그래서 같은 시각이 아니라 같은 좀비 수에서 비교합니다.

좀비 수별 비교 (구간 안의 초당 기록 평균):

| 좀비 수 | 엔진 | 표본(초) | 물리 단계/초 | 단계 시간 | FPS | 프레임 시간 |
| --- | --- | ---: | ---: | ---: | ---: | ---: |
| 25,000~35,000 | 원작 | 19 | 6.4 | 59.2 ms | 32.5 | 31.2 ms |
| | Chipmunk 다중 코어 | 14 | 8.6 | 14.1 ms | 31.9 | 31.8 ms |
| | Rapier (AVX2) | 16 | 7.4 | 32.7 ms | 32.3 | 31.5 ms |
| 40,000~50,000 | 원작 | 45 | 3.3 | 211.1 ms | 32.6 | 31.3 ms |
| | Chipmunk 다중 코어 | 19 | 7.8 | 26.2 ms | 31.4 | 32.5 ms |
| | Rapier (AVX2) | 23 | 6.5 | 49.6 ms | 27.1 | 37.9 ms |
| 60,000~70,000 | 원작 | 83 | 1.8 | 464.4 ms | 25.2 | 40.7 ms |
| | Chipmunk 다중 코어 | 26 | 6.2 | 55.8 ms | 20.5 | 50.6 ms |
| | Rapier (AVX2) | 30 | 5.2 | 82.5 ms | 16.6 | 62.1 ms |
| 85,000~95,000 | 원작 | — | — | — | — | — |
| | Chipmunk 다중 코어 | 39 | 5.0 | 92.4 ms | 12.5 | 83.7 ms |
| | Rapier (AVX2) | 50 | 4.0 | 141.2 ms | 10.7 | 99.3 ms |

시간 구간별 기록 (웨이브 발생 시점부터, 구간 평균. 최대값은 구간 안의 가장 긴 1회):

| 구간 | 엔진 | 좀비 수 | FPS | 프레임 시간 | 프레임 최대 | 물리 단계/초 | 단계 시간 | 단계 최대 |
| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 0~30초 | 원작 | 28,102 | 32.5 | 31.3 ms | 123 ms | 6.8 | 51.2 ms | 113 ms |
| | Chipmunk 다중 코어 | 30,111 | 32.0 | 31.7 ms | 89 ms | 8.7 | 14.2 ms | 24 ms |
| | Rapier (AVX2) | 28,751 | 32.3 | 31.5 ms | 96 ms | 7.6 | 30.5 ms | 44 ms |
| 30~60초 | 원작 | 40,813 | 32.5 | 31.3 ms | 81 ms | 4.1 | 151.9 ms | 215 ms |
| | Chipmunk 다중 코어 | 48,248 | 29.5 | 34.8 ms | 67 ms | 7.5 | 30.4 ms | 45 ms |
| | Rapier (AVX2) | 44,897 | 27.4 | 37.7 ms | 64 ms | 6.5 | 49.2 ms | 63 ms |
| 60~120초 | 원작 | 50,547 | 32.2 | 31.6 ms | 62 ms | 2.7 | 278.6 ms | 357 ms |
| | Chipmunk 다중 코어 | 67,592 | 19.8 | 53.2 ms | 92 ms | 6.1 | 58.8 ms | 78 ms |
| | Rapier (AVX2) | 62,240 | 18.1 | 57.9 ms | 133 ms | 5.4 | 77.1 ms | 104 ms |
| 120~180초 | 원작 | 59,697 | 27.9 | 36.6 ms | 74 ms | 2.0 | 398.1 ms | 458 ms |
| | Chipmunk 다중 코어 | 86,770 | 13.3 | 78.7 ms | 126 ms | 5.1 | 87.5 ms | 111 ms |
| | Rapier (AVX2) | 79,772 | 12.7 | 83.2 ms | 228 ms | 4.4 | 114.1 ms | 150 ms |
| 180~240초 | 원작 | 66,580 | 24.3 | 42.0 ms | 94 ms | 1.7 | 485.9 ms | 530 ms |
| | Chipmunk 다중 코어 | 99,558 | 10.5 | 100.2 ms | 154 ms | 4.6 | 112.1 ms | 138 ms |
| | Rapier (AVX2) | 91,784 | 10.5 | 102.2 ms | 187 ms | 3.9 | 146.2 ms | 181 ms |

- 물리 단계/초가 게임 세계가 진행되는 속도입니다. 원작은 6~7만 마리에서 초당 1.8단계로 떨어져 게임이 크게 느려지고, 그래서 4분 동안 좀비가 6.7만 마리까지만 늘었습니다. 원작의 FPS가 높게 나오는 것은 게임 진행 자체가 느려졌기 때문입니다.
- 6~7만 마리에서 단계 시간은 원작 464 ms, Chipmunk 다중 코어 56 ms(약 8배 빠름), Rapier 83 ms(약 5.6배 빠름)입니다.
- Rapier는 모든 구간에서 Chipmunk 다중 코어보다 단계 시간이 1.5~2.3배 깁니다. 가장 긴 프레임도 Rapier가 더 깁니다(최대 228 ms, Chipmunk 154 ms).
- 9만 마리 근처에서는 Chipmunk 다중 코어도 초당 5단계 정도라 게임이 제 속도로 돌지 않습니다.
- 이 측정에서는 각 엔진의 `DXPhysicsNative.dll`(Rapier는 `rapier2d_ffi.dll`·`rapier2d_ffi_avx2.dll`과 함께)을 게임 폴더에 넣었습니다. 지금 방식(모드 폴더에서 연결)과 단계 시간은 같습니다([물리 DLL 연결 방식](#물리-dll-연결-방식)).

같은 측정은 벤치 스크립트로 바로 할 수 있습니다. 엔진마다 게임을 한 번씩 띄워 자동 시험을 돌리고 결과를 정리합니다.

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\bench.ps1 -Engine chipmunk,rapier,original -Seconds 240
```

- `-Engine`(쉼표로 여러 개, 기본 `chipmunk,rapier`), `-Runs`(엔진마다 반복 횟수), `-Seconds`, `-Factor`, `-Delay`, `-Seed`, `-Threads`, `-Zoom max|off`가 아래 설정과 같은 뜻입니다.
- 결과는 `bench-out\<날짜-시각>\`에 실행별 CSV(`<엔진>-<회차>.csv`)와 `summary.csv`(최대 좀비 수, 평균 단계·프레임 시간, 마지막 30초 평균)로 남습니다. `-OutDir`로 바꿀 수 있습니다.
- 설정은 환경 변수로 게임에 넘기므로 `engine.json`은 바꾸지 않습니다. 게임은 보통 Steam을 거쳐 다시 시작되며 이때 환경 변수가 사라지므로, 스크립트가 실행하는 동안만 게임 폴더에 `steam_appid.txt`를 만들어 바로 실행되게 합니다(원래 있던 파일은 그대로 둡니다). Steam이 켜져 있어야 합니다.

직접 설정하려면 `engine.json`의 성능 시험 설정을 씁니다. `bench`만 켜면 게임 중 Ctrl+Shift+F9로 마지막 웨이브를 바로 발생시키고, `benchAuto`까지 켜면 시작 화면에서 위 생존 게임을 자동으로 시작하고 정해진 시간 뒤 게임을 끕니다. 기록은 `%APPDATA%\TABPerf\bench.csv`에 남습니다.

```json
{
  "bench": true,
  "benchAuto": true,
  "benchSwarmFactor": 4,
  "benchSeconds": 240,
  "benchSwarmDelay": 5,
  "benchSeed": 20261104
}
```

| 키 | 기본값 | 의미 |
| --- | --- | --- |
| `bench` | `false` | 성능 시험 모드. Ctrl+Shift+F9 트리거와 초당 기록 |
| `benchAuto` | `false` | 자동 시험. 생존 게임 자동 시작, 피해 끔, 웨이브 발생, 시간이 되면 게임 종료 |
| `benchSwarmFactor` | `4` | 마지막 웨이브를 몇 번 발생시킬지(1~20) |
| `benchSeconds` | `240` | 웨이브 발생 뒤 기록할 시간(초, 30 이상) |
| `benchSwarmDelay` | `5` | 레벨 시작부터 웨이브 발생까지(초) |
| `benchSeed` | `20261104` | 생존 게임 맵 시드 |
| `benchThreads` | 자동 | 물리 작업 스레드 수(`DX_SetOption(0)`). 레벨 시작 때 적용 |
| `benchZoom` | `"max"` | 자동 시험의 카메라. `max`는 게임이 허용하는 가장 넓은 화면으로 고정, `off`는 손대지 않음 |
| `benchCsv` | `%APPDATA%\TABPerf\bench.csv` | 초당 기록 파일 경로 |

모든 키는 환경 변수로도 줄 수 있고, 환경 변수가 `engine.json`보다 우선합니다. 켜기/끄기 값은 `1`/`0`, `on`/`off`, `true`/`false`, `yes`/`no`를 받습니다.

| 키 | 환경 변수 |
| --- | --- |
| `physicsEngine` | `TABPERF_ENGINE` |
| `bench` | `TABPERF_BENCH` |
| `benchAuto` | `TABPERF_BENCH_AUTO` |
| `benchSwarmFactor` | `TABPERF_BENCH_FACTOR` |
| `benchSeconds` | `TABPERF_BENCH_SECONDS` |
| `benchSwarmDelay` | `TABPERF_BENCH_DELAY` |
| `benchSeed` | `TABPERF_BENCH_SEED` |
| `benchThreads` | `TABPERF_BENCH_THREADS` |
| `benchZoom` | `TABPERF_BENCH_ZOOM` |
| `benchCsv` | `TABPERF_BENCH_CSV` |

자동 시험은 시작 애니메이션이 끝나 입력이 켜지면 카메라를 게임의 최대 축소 폭(마우스 휠로 뺄 수 있는 끝)으로 맞추고 시험 내내 유지합니다. Rapier 판이면 `bench.csv`에 단계 구간별 시간(`r_push_ms` 넣기, `r_step_ms` Rapier 단계, `r_pull_ms` 가져오기, `r_pairs_ms` 접촉 쌍, `r_index_ms` 질의 인덱스)도 남습니다.

### 비교 도구 (`compare.exe`, 장면 3개 × 30단계)

밀집 장면(몸체가 40×40 영역에 몰린 경우)의 단계당 시간입니다.

| 몸체 수 | 원작 | Chipmunk 다중 코어 | Rapier (AVX2) |
| ---: | ---: | ---: | ---: |
| 2,000 | 2.65 ms | 3.65 ms | 5.37 ms |
| 6,000 | 26.45 ms | 12.51 ms | 24.89 ms |
| 12,000 | 199.26 ms | 39.08 ms | 68.06 ms |
| 25,000 | 1,059.82 ms | 138.50 ms | 287.32 ms |

작업 스레드 수에 따른 변화 (몸체 12,000개, 밀집 장면):

| 작업 스레드 | Chipmunk 다중 코어 | Rapier |
| ---: | ---: | ---: |
| 0 (단일 스레드) | 182.9 ms | 145.7 ms |
| 3 | 49.6 ms | 68.6 ms |
| 7 | 42.3 ms | 69.1 ms |
| 15 | 38.8 ms | 67.8 ms |
| 자동 | 39.2 ms | 69.3 ms |

단일 스레드에서는 Rapier가 Chipmunk보다 20% 빠릅니다. 그런데 Rapier는 스레드 4개 정도에서 더 빨라지지 않고, Chipmunk 다중 코어는 스레드를 늘릴수록 빨라집니다(단일 스레드 대비 Chipmunk 4.7배, Rapier 2.1배). 이 장면은 몸체가 거의 모두 맞닿아 있어 Rapier가 하나의 큰 섬(island)으로 풀어야 하는 경우입니다.

## 소스 구성

```text
mod/                   C# 엔진 최적화 모드와 모드 정보
native/
├─ chipmunk/            수정된 Chipmunk2D 7.0.0
├─ dxphysics/           게임용 물리 API 래퍼, 작업 스레드 풀, 오류 진단
├─ rapier/              Rapier 물리 DLL (DXPhysicsRapier.c)
└─ harness/             원작 DLL과의 비교 도구
scripts/
├─ setup.ps1            게임 확인, TABModLoader 참조 파일 받기(refs/), 모드로더 패치
├─ build.ps1            물리 엔진(Chipmunk·Rapier), 비교 도구, 엔진 모드 빌드 (-Target으로 선택)
├─ install.ps1          빌드한 모드와 물리 엔진을 Mods\TABPerf에 설치·제거
├─ bench.ps1            게임 안 자동 성능 시험을 엔진별로 돌리고 결과 정리 (bench-out/)
└─ lib/
   ├─ game.ps1          게임 폴더 찾기, 모드로더 패치 확인, 파일 해시
   └─ toolchain.ps1     MSVC·csc 찾기, 빌드 명령 실행, 패딩 검사
README-SOURCE.txt      원본 소스 설명 및 구현 상세
LICENSE.txt            패치 코드의 MIT 라이선스
```

물리 DLL을 수정할 때는 `native/dxphysics/dxdiag.c`의 `dx_text_pad`를 유지해야 합니다. TABModLoader가 DLL 시작 주소의 `+0x13B3` 위치에 5바이트를 덮어쓰기 때문에, 해당 공간에 실제 코드가 배치되면 게임이 종료될 수 있습니다. 빌드 스크립트는 두 물리 DLL 모두 링크 맵에서 이 위치를 검사하고, 어긋나면 빌드를 실패시킵니다. Rapier 판은 같은 이유로 Rust 코드를 `DXPhysicsNative.dll`에 정적으로 넣지 않고, 별도의 `rapier2d_ffi.dll`에서 지연 로드합니다. Rust 코드는 일반 `.text` 섹션에 들어가 패딩보다 앞에 놓이기 때문입니다.

비교 도구의 기본 사용법은 다음과 같습니다.

```powershell
.\native\out\compare.exe "C:\Original\DXPhysicsNative.dll" ".\native\out\DXPhysicsNative.dll"
```

추가 인자는 `[장면|all] [몸체 수] [단계 수]`입니다. 원본 소스 안내에 따르면 병렬 옵션 `3`, `5`, `6`, `8`을 끈 조건에서 원작 DLL과 위치·속도의 비트 단위 일치를 확인했습니다. 기본 병렬 설정 전체에 대한 동일성 보장을 뜻하지는 않습니다.

## 제작 및 라이선스

- 제작·배포: **똘똘똘이 (`ddolddolddol-e`)** — [치지직 채널](https://chzzk.naver.com/1906dd57f578c255feca54700bcccfc9)
- 패치 코드: [MIT License](LICENSE.txt), Copyright (c) 2026 ddolddolddol-e
- Chipmunk2D: 별도의 [MIT License](native/chipmunk/LICENSE.txt)
- Rapier: [Apache License 2.0](https://github.com/dimforge/rapier/blob/master/LICENSE). 저장소에 포함하지 않고 빌드할 때 받습니다. 빌드한 `rapier2d_ffi.dll`·`rapier2d_ffi_avx2.dll`을 배포할 때는 이 라이선스 문구를 함께 넣어야 합니다.

MIT 라이선스 조건에 따라 수정 및 재배포할 수 있으며, 저작권 고지와 라이선스 문구를 유지해야 합니다.

They Are Billions는 **Numantian Games**의 게임입니다. 이 패치는 비공식 모드이며 Numantian Games와 제휴하지 않습니다.
