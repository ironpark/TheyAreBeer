# 변경 기록

이 프로젝트의 주요 변경 사항을 기록합니다. 형식은 [Keep a Changelog](https://keepachangelog.com/ko/1.1.0/)를 따릅니다. 구현 방식과 측정·실험 결과는 [개발 문서](docs/DEVELOPMENT.md)에 있습니다.

## [Unreleased]

## [0.0.1] - 2026-11-05

첫 릴리스입니다. 최초 배포본(`ef035e1`) 이후의 변경을 모두 담습니다.

### 최적화 탭

- 게임 옵션 창에 **최적화** 탭을 더했습니다(조작 탭 다음). 물리 엔진, 렌더러, DXVK 최대 FPS, 빠른 타이머 루프, 로직 따라잡기를 고르면 적용을 누를 때 `engine.json`에 저장합니다. 다시 시작해야 하는 설정이 바뀌면 게임의 다시 시작 안내가 뜹니다. 그래픽 탭에 있던 **좀비 체력바 가볍게**도 이 탭으로 옮겼고, 이제 적용을 누를 때 바뀝니다(취소하면 그대로).

### 그래픽과 배포

#### 변경
- 그래픽을 `engine.json`의 `"renderer"`로 고릅니다. `dxvk`(기본)는 모드 폴더의 DXVK, `d3d9`는 시스템 Direct3D 9입니다. 예전의 `"dxvk": true/false`는 더 읽지 않고 설치 스크립트가 지웁니다. 환경 변수는 `TABPERF_RENDERER`입니다(`TABPERF_DXVK` 대신).
- `scripts\install.ps1`이 DXVK를 기본으로 받아 넣습니다(`-NoDxvk`로 뺌). `-Dxvk on|off` 대신 `-Renderer dxvk|d3d9`를 씁니다.
- 모드 버전을 0.0.1로 정했습니다.

#### 추가
- `scripts\package.ps1`: 배포용 `dist\TABPerf-v<버전>.zip`을 만듭니다. 모드, 두 물리 엔진, DXVK, 라이선스 문서가 들어갑니다.
- GitHub Actions(`.github/workflows/build.yml`): Windows에서 전체 빌드와 패키지를 하고, `v*` 태그를 푸시하면 릴리스를 만듭니다. ([빌드 문서](docs/BUILD.md#배포-파일))
- 측정 도구 `TABPERF_RENDERDUMP=1`: 게임 렌더러의 구조와 쓰는 SlimDX 기능을 파일로 남깁니다(렌더러 교체 검토용).
- D3D9On12(Direct3D 9를 Direct3D 12로)는 시험한 뒤 넣지 않았습니다. 창이 약 800×600보다 크면 `Present`는 성공해도 화면에 아무것도 나오지 않았습니다. 시스템 D3D9, D3D11, D3D12, D3D9Ex FLIPEX는 같은 크기에서 정상이었습니다(원격 데스크톱에서 확인).

- 전반적인 안정성 향상
- 빌드 스크립트와 도구 개선
- 물리 엔진 교체 기능 추가 ([Rapier](https://rapier.rs/) 2D)

### Rapier 물리 엔진 (`native/rapier/`)

#### 추가
- `native/rapier/DXPhysicsRapier.c`: 게임이 부르는 물리 함수를 Rapier 2D로 구현한 `DXPhysicsNative.dll`입니다. Rapier [공식 C 바인딩](https://github.com/dimforge/rapier/tree/master/bindings/c)(`rapier2d_ffi.dll`)을 지연 로드합니다. 구조는 [개발 문서](docs/DEVELOPMENT.md#rapier-래퍼-구조) 참고.
  - 충돌 필터·충돌 시작/분리 콜백·접촉 무시를 Rapier 충돌 그룹과 이벤트로 처리합니다. 운동학 몸체 사이의 충돌도 Chipmunk처럼 알립니다.
  - 몸체 잠자기(sleep)를 켭니다(`DX_RAPIER_SLEEP=0`으로 끔).
  - CPU에 맞춰 AVX-512 → AVX2·FMA → 기본 빌드 순으로 골라 불러옵니다(`DX_RAPIER_AVX512=0`, `DX_RAPIER_AVX2=0`).
  - 몸체 값을 단계마다 한 번의 일괄 호출로 넘깁니다. 실제 게임 5만 마리에서 단계가 60.5 ms에서 약 56 ms로 줄었습니다(`DX_RAPIER_BATCH=0`으로 끔).
  - 단계 뒤에는 깨어 있는 몸체만 읽어 옵니다. 값이 NaN이 되어 격리된 몸체는 마지막 정상 위치에서 되살립니다.
  - 접촉 강성을 게임의 긴 단계(1/12초)에 맞춥니다. 밀집 장면에서 단계가 6~18% 빨라지고 벽 밖으로 밀리는 몸체가 크게 줄었습니다(`DX_RAPIER_SOFTNESS=0`으로 끔).
  - 넣기·가져오기·질의 인덱스를 작업 스레드로 나눠 처리합니다. 결과는 그대로입니다.
  - 몸체를 만들 때 질의 인덱스 락을 기다리지 않습니다. 좀비 6~7만에서 몸체 하나에 최대 190 µs 걸리던 것이 약 1.7 µs가 되어, 물리 스레드의 대기 작업이 단계당 8.8 ms에서 0.2 ms로 줄었습니다.
  - 하위 단계(솔버 반복) 수를 몸체가 몰리는 정도에 따라 5~20 사이에서 자동으로 조절합니다. 적으면 몸체가 겹쳐 오히려 느려지기 때문입니다. 실제 게임 6.8만 마리에서 단계가 69.5 ms에서 65.0 ms로 줄었습니다. `DX_RAPIER_ITERATIONS`로 고정하고 `DX_RAPIER_ADAPT=0`으로 끕니다. ([실험](docs/DEVELOPMENT.md#하위-단계-수))
  - Rapier 내부 구간 시간을 잴 수 있습니다(`DX_RAPIER_COUNTERS=1`, 단계 약 7% 증가, 기본 끔).
  - 시험용 스위치: 하위 단계 안 반복 수(`DX_RAPIER_PGS`·`DX_RAPIER_STAB`), 접촉 파라미터(`DX_RAPIER_SLOP` 등), CCD 끄기(`DX_RAPIER_CCD=0`), soft CCD(`DX_RAPIER_SOFTCCD`), CCD 나누기(`DX_RAPIER_CCD_SPLIT`). 모두 기본값보다 나은 결과가 없어 기본은 끔입니다. ([실험](docs/DEVELOPMENT.md#성능-실험))
  - 불러온 Rapier 라이브러리가 디버그 빌드면 로그에 경고를 남깁니다.
  - 질의(영역·반경·가장 가까운 몸체·레이캐스트·남은 공간)는 자체 격자 인덱스로 처리해 여러 스레드에서 동시에 불러도 안전합니다.
  - 크래시 진단과 모드로더용 코드 패딩은 Chipmunk 판과 같은 `dxdiag.c`를 씁니다.
- Rapier 판 빌드(`scripts\build.ps1 -Target rapier`): Rapier를 고정 커밋으로 받아 패치(`native\rapier\*.patch`)를 적용하고, 공식 CMake 빌드로 기본·AVX2·AVX-512 세 가지를 만들어 통합 테스트를 돌립니다. Rust는 fat LTO로 빌드합니다. 패치 목록이 바뀌면 소스를 되돌린 뒤 다시 적용합니다. 16레인 솔버는 8레인과 차이가 없어 뺐습니다. ([상세](docs/DEVELOPMENT.md#rapier-빌드와-패치))
- 엔진 모드: `engine.json`의 `"physicsEngine": "rapier"`로 Rapier 판을 씁니다. 로그에 쓰인 엔진을 남깁니다.
- 성능 시험 모드(`mod/Diag/Bench.cs`, `engine.json`의 `"bench": true`): 사용법은 [성능 시험 문서](docs/BENCH.md).
  - Ctrl+Shift+F9로 마지막 웨이브를 바로 발생시킵니다.
  - 1초마다 프레임·물리 단계·좀비 수와 단계 구간, 게임 쪽 처리, 게임 시계·로직 갱신, CPU 사용량을 CSV에 남깁니다.
  - `benchAuto`: 900% 생존 게임을 자동으로 시작해 카메라를 최대로 빼고, 피해를 끈 뒤 마지막 웨이브를 발생시키고, 정해진 시간 뒤 게임을 끕니다.
  - 모든 설정을 환경 변수로도 줄 수 있고, 환경 변수가 `engine.json`보다 우선합니다.
  - 줌: 시작 애니메이션이 끝나 입력이 켜진 뒤 카메라 폭을 게임의 최대값으로 맞추고 유지합니다. 전에는 애니메이션 중에 줌을 넣어 적용되지 않을 수 있었습니다(시험에서 24 → 80).

### 물리 DLL 연결과 설치

#### 수정
- **모드 폴더의 물리 DLL이 쓰이지 않던 문제**: 게임은 모드 폴더의 대체본을 불러 두어도 게임 폴더의 원작 DLL을 썼습니다. `mod/Physics/PhysicsRedirect.cs`가 게임의 물리 호출을 실행 중에 대체본으로 돌립니다. 게임 파일은 바꾸지 않고, 하나라도 실패하면 모두 되돌려 게임 폴더의 DLL을 씁니다. ([상세](docs/DEVELOPMENT.md#연결-방식))
- **로그의 물리 DLL 표시**: 게임이 실제로 부르는 DLL을 보고 표시합니다. 전에는 게임이 원작을 쓰는데 대체본으로 나올 수 있었습니다.
- **물리 계산 중 게임 시계가 멈추던 문제**: 게임 옵션 FastTimming이 꺼져 있으면 원작은 물리 계산 동안 게임 시계를 멈춰, 좀비 7만 마리에서 게임이 0.5배속으로 돌았습니다. 모드가 이 옵션과 상관없이 빠른 타이머 방식으로 게임 시계를 돌립니다. 같은 시험에서 5만 마리까지 1배속, 7만 마리에서 Chipmunk 판 0.92배·Rapier 판 0.82배였습니다. 게임 옵션 값은 바꾸지 않고, `engine.json`의 `"fastTiming": false`로 원래대로 돌립니다. ([상세](docs/DEVELOPMENT.md#게임-시계와-로직-갱신))
- **게임 물리 호출 비용**: 게임은 좀비마다 단계 앞뒤로 물리 값을 여러 번 읽고 씁니다(7만 마리면 단계당 50만 번이 넘음). 호출마다의 권한 검사를 없애고(`TABPERF_SUCS=0`으로 끔), Rapier 판에서는 단순한 값 읽기·쓰기를 DLL 호출 없이 처리합니다(`TABPERF_FASTBODY=0`으로 끔). 좀비 6~8만에서 단계 앞뒤 처리가 10.1 ms에서 약 3.4 ms로 줄었습니다. ([상세](docs/DEVELOPMENT.md#호출-비용-줄이기))

#### 추가
- 엔진은 모드 폴더의 `chipmunk\`, `rapier\` 하위 폴더에 둡니다. `engine.json`의 `physicsEngine`으로 `chipmunk`(기본)·`rapier`·`original`을 고릅니다. 고른 엔진을 쓸 수 없으면 Chipmunk 판, 그것도 안 되면 원작 DLL을 씁니다.
- `scripts\install.ps1 [-Engine chipmunk|rapier|original] [-Uninstall]`: 모드와 빌드한 엔진을 `Mods\TABPerf`에 복사합니다. `-Engine`은 `engine.json`의 `physicsEngine`도 정합니다. 예전 설치가 남긴 `DXPhysicsNative.dll.orig`가 있으면 게임 폴더의 DLL을 원작으로 되돌리고, 모드 폴더 바로 아래의 물리 DLL은 지웁니다.

### 스크립트 (`scripts/`)

#### 변경
- 빌드 스크립트 세 개(`build-native.ps1`, `build-rapier.ps1`, `build-mod.ps1`)를 `scripts\build.ps1` 하나로 합쳤습니다. `-Target chipmunk|harness|rapier|mod`(쉼표로 여러 개)로 고르고, 생략하면 모두 빌드합니다. 이때 비교 도구 빌드 실패는 경고로, Rapier 도구(git, cmake, rustup)가 없으면 건너뜁니다.
- 공용 함수를 `scripts\lib\`로 나눴습니다. `game.ps1`은 게임 폴더 찾기·모드로더 패치 확인·해시, `toolchain.ps1`은 MSVC·csc 찾기·빌드 명령 실행·패딩 검사를 맡습니다. `setup.ps1`과 `install.ps1`의 중복 함수(해시, 로더 확인, 게임 폴더 확인)도 여기로 옮겼습니다.
- 빌드 명령 실행(`Invoke-Cmd`)을 한 곳에서 처리합니다. 전에는 스크립트마다 오류 처리 방식(`$ErrorActionPreference` 복원 여부)이 달랐습니다.
- Rapier CMake 옵션을 한 표에서 만들어, CMake 인자와 빌드 뒤 캐시 확인이 같은 값을 씁니다.
- `setup.ps1`은 기본으로 모드가 맞춰진 TABModLoader v1.1.6을 받습니다(`-Version latest`면 최신). 참조 라이브러리는 릴리스 버전 이하의 `Lib*` 폴더 중 그 파일이 있는 가장 새 폴더에서 받습니다. v1.1.7의 `Lib`에는 `DXVision.dll`만 있어 `0Harmony.dll`을 찾지 못하고 실패했습니다.

#### 추가
- `scripts\bench.ps1`: 게임 안 자동 성능 시험을 엔진별·회차별로 돌리고, 실행별 CSV와 `summary.csv`를 `bench-out\`에 남깁니다. 사용법은 [성능 시험 문서](docs/BENCH.md).

### 물리 DLL (`native/`)

#### 수정
- **dt = 0 단계에서 해제된 메모리 사용**: `Space_Step(space, 0)`이 접촉 정리 없이 지울 몸체를 해제해, 캐시된 arbiter가 해제된 몸체와 모양을 가리킬 수 있었습니다. 이제 dt가 0이면 지울 몸체 목록을 건드리지 않습니다.
- **맵을 넘길 때 몸체 누수**: 이전 공간에서 지우기로 예약된 몸체가 새 `Space_Create` 뒤에는 영원히 해제되지 않았습니다. 이제 새 공간을 만들 때 바로 해제합니다. 해제 전에 userData를 비워, 내려간 맵의 엔티티에 대한 콜백이 게임으로 가지 않습니다.
- **지우기 지연이 생략하던 정리 작업**: constraint가 있거나 잠든 몸체, 또는 활성 공간이 아닌 몸체는 예약하지 않고 바로 지웁니다. 예약 목록에는 활성 공간의 몸체만 들어가므로, 단계마다 공간별로 거르는 과정이 없어졌습니다.
- **`DX_SetOption(0, n)`이 무시되던 문제**: 직접 지정한 스레드 수를 `Space_Create`가 덮어쓰지 않습니다.
- **스레드 풀 직렬 경로**: 직렬로 돌 때도 조각(chunk) 단위로 호출합니다. 조각 번호로 출력을 나누는 호출부(병렬 충돌 탐색, 해시 집합 거르기)가 이전 실행의 출력을 다시 읽던 잠재 버그를 막습니다.
- **스레드 풀 재진입과 동시 호출**: 중첩되거나 동시에 들어온 `dx_parallel_for`는 직렬로 처리합니다. 작업 중에는 스레드 수를 바꾸지 않습니다.
- **여러 공간이 공유하던 전역 상태**: 병렬 접촉 기록용 링 버퍼는 `cpSpace`별로, BBTree 재질의 플래그는 트리별로 옮겼습니다.
- **블록 단위 충격 풀이**: 블록 크기가 몸체 중심에서 모양 경계까지의 최대 거리도 반영합니다. 중심에서 벗어난 모양이나 모양이 여러 개인 몸체에서도 같은 단계의 블록끼리 몸체를 공유하지 않습니다. 게임의 원·상자 모양에서는 값이 그대로입니다.
- **병렬 접촉 기록**:
  - wildcard 기본 핸들러를 쓰면 지름길 경로를 타지 않습니다.
  - `collisionPersistence`가 링 크기 이상이면 병렬 기록을 쓰지 않습니다.
  - 링 버퍼가 커질 때 `realloc`으로 이전 블록을 해제하지 않습니다. upstream 접촉 버퍼처럼 공간이 살아 있는 동안 유지하고(`allocatedBuffers`), 공간을 지울 때 해제합니다. 계속 남는 arbiter가 해제된 메모리를 가리키지 않습니다.
- **격자 기반 충돌 탐색**: 유효한 모양이 없을 때, 격자 칸 수나 칸별 항목 수가 너무 클 때는 직렬 경로로 넘어갑니다.
- **크래시 진단 (`dxdiag.c`)**:
  - 저장 경로를 미리 계산해 둡니다.
  - 파일을 버퍼 없이 씁니다.
  - 보고서 작성 중 예외가 나도 그때까지 쓴 내용은 남습니다(`__try`).
  - 물리·작업 스레드에 `SetThreadStackGuarantee`를 호출해, 스택 오버플로도 기록합니다.
- **입력 처리**:
  - `DX_PHYS_THREADS`를 `GetEnvironmentVariableA`로 읽습니다. 값의 형식이 잘못되면 자동 값을 씁니다.
  - 지우기 예약 목록의 `realloc` 실패를 처리합니다.
  - `Body_SetAngle`이 그 몸체가 속한 공간에서 다시 색인합니다.

#### 변경
- 병렬 충돌 탐색이 모양마다 격자 칸 범위를 한 번만 계산합니다. 이전에는 같은 계산을 두 번씩 했습니다.
- 크래시 진단은 첫 `Space_Create` 뒤부터 기록합니다. 그 전에는 저장 경로가 정해지지 않아 기록하지 않습니다.
- `dx_text_pad`가 링크에서 빠지지 않게 `/INCLUDE:dx_text_pad`를 추가했습니다.
- 작업 스레드를 만들 때 DLL을 고정(pin)해, 언로드 뒤 작업 스레드가 해제된 코드를 실행하는 일이 없게 했습니다.

### 엔진 모드 (`mod/`)

> `scripts\setup.ps1`로 받은 TABModLoader v1.1.6 참조 파일로 빌드해, 컴파일되는 것까지 확인했습니다. 게임 안에서는 아직 실행해 보지 않았습니다.

#### 수정
- **FarField**:
  - 거리 지도를 하나의 변경 불가 객체로 묶어 한 번에 교체합니다. 서로 다른 시점의 배열과 크기를 섞어 읽다가 범위를 벗어나는 일이 없습니다.
  - 세대 번호를 추가했습니다. 맵을 내린 뒤 끝난 분류 작업이 새 맵에 이전 맵의 거리 지도를 쓰지 않습니다.
  - 분류 스냅샷은 `Monitor.TryEnter`로 잠금을 시도하고, 못 얻으면 그 초를 건너뜁니다. 메인 스레드가 1초마다 멈칫하던 것을 줄입니다.
  - `NearestPre`와 `WallPre`는 예외가 나면 게임으로 넘기지 않고 기록한 뒤 원작을 실행합니다.
  - 맵을 내릴 때 이동 작업을 기다리고 `_born`을 비웁니다.
- **PhysicsTime**:
  - `OnFinishPhysics`를 `finally`에서 호출합니다.
  - 그냥 삼키던 예외를 기록합니다. 기록 횟수는 `Log.Error`가 제한합니다.
  - `PostMethods`를 매 단계 다시 읽습니다. 값이 없거나 `ExecuteAll` 메서드가 없으면 그 단계는 원작 처리로 넘깁니다. 설치할 때 `ExecuteAll`을 찾지 못하면 이 손질을 설치하지 않습니다.
- **EnergyFix**:
  - 원작 길 찾기와 답이 처음 달라지면 번호표 방식을 끄고, 이후로는 원작 길 찾기를 씁니다.
  - 아무도 읽지 않던 유닛 길 찾기 측정(`MeasureUnits`)을 기본으로 끕니다.
- **ZSort**: 맵을 내릴 때 상태를 초기화합니다. 몰아서 정렬하다 원작 방식으로 돌아간 다음 프레임에 한 번 더 정렬합니다.
- **SmoothTime**: 시작 중 실패하면 이미 시작한 시간 작업을 정리하고 원작으로 넘깁니다. 시간 작업이 두 개 돌아 게임이 2배속이 되는 일을 막습니다. `timeBeginPeriod(1)`은 한 번만 호출합니다.
- **GameOptions**: `File.Replace`로 설정을 저장해, 저장 중 실패해도 설정이 사라지지 않습니다. 옵션 창을 다시 열어도 체크박스 줄이 중복되지 않습니다.
- **Log**: 같은 오류는 10초에 3건까지만 기록하고, 생략한 건수를 다음 기록에 덧붙입니다.

### 빌드와 도구

#### 추가
- `scripts\setup.ps1`: 세팅 스크립트를 추가했습니다.
  - **게임 설치 확인**: Steam 라이브러리(레지스트리, `libraryfolders.vdf`, `appmanifest_644930.acf`)에서 They Are Billions를 찾습니다. `-GameDir`로 직접 지정할 수도 있습니다.
  - **참조 파일**: [TABModLoader](https://codeberg.org/JKstring/TABModLoader) 릴리스와 저장소에서 빌드용 참조 파일(`TheyAreBillions.exe`, `DXVision.dll`, `0Harmony.dll`)을 `refs\`에 받습니다. 받은 파일의 SHA256을 보여 주고, 로더 버전이 `ModInfos.json`의 요구 버전과 다르면 경고합니다. `build-mod.ps1`은 이제 기본으로 `refs\`를 씁니다(`-RefDir` 생략 가능).
  - **모드로더 자동 패치**: 게임 실행 파일에 로더(`AX.ModLoader`)가 들어 있는지 확인합니다. 없으면 원본을 `TheyAreBillions.exe.orig`로 보관하고 패치합니다. 다른 로더 버전으로 패치돼 있으면 받은 버전으로 바꿉니다. 게임이 실행 중이거나 쓰기 권한이 없으면 멈추고 안내합니다. 패치 후에는 해시를 검증합니다. `-NoPatch`를 주면 상태만 보여 줍니다.
- `build-native.ps1`이 링크 맵(`DXPhysicsNative.map`)을 만듭니다. TABModLoader가 덮어쓰는 `+0x13B3`이 `dx_text_pad` 안에 있는지 검사해, 벗어나면 빌드를 실패시킵니다.
- `compare.exe`는 몸체 수가 병렬 경로 기준(2,048개)보다 적으면, 직렬 경로만 비교한다고 경고합니다.
- `compare.exe prof <Rapier DLL>`이 Rapier 판의 단계 구간과 내부 구간 시간을 보여 줍니다.

#### 변경
- 빌드 스크립트를 `scripts/` 폴더로 옮겼습니다: `scripts\build-native.ps1`, `scripts\build-mod.ps1`. 스크립트는 저장소 루트를 기준으로 경로를 찾으므로, 어느 폴더에서 실행해도 됩니다. `README.md`와 `README-SOURCE.txt`의 빌드 명령도 새 경로로 고쳤습니다.
- `.gitignore`에 `/refs/`를 추가했습니다. 받은 게임과 로더 파일이 커밋되지 않습니다.
- `build-mod.ps1`은 참조 파일이 없으면 `scripts\setup.ps1`을 실행하라고 안내합니다.

#### 수정
- `build-native.ps1`:
  - 빌드 전에 이전 DLL을 지웁니다. 빌드가 실패하면 예전 DLL이 남지 않습니다.
  - `vcvars64.bat`이 있는지 확인합니다.
  - 비교 도구 빌드가 실패하면 경고를 띄웁니다.
- `build-mod.ps1`: 자동 변수 `$args`에 대입하던 것을 `$cscArgs`로 바꿨습니다.
- `compare.exe`:
  - 한글 등 ANSI 경로의 DLL을 열 수 있습니다(`CP_UTF8` 대신 `CP_ACP`).
  - 파일 열기 실패를 처리합니다.

### Rapier 솔버 대기

#### 변경
- `native/rapier/rapier-solver-park.patch`: Rapier 병렬 솔버가 단계마다 다른 작업 스레드를 기다릴 때 계속 돌지 않고, 2,000번 돈 뒤 잠듭니다(`RAPIER_SYNC_SPIN`, 0이면 원래대로). 실제 게임 좀비 6~9만 마리에서 게임 시간이 3~6% 빨라졌습니다. 자세한 측정은 [개발 문서](docs/DEVELOPMENT.md#솔버-대기)에 있습니다.

### Rapier 레이캐스트

#### 변경
- `Space_RayCast`가 선분을 감싸는 상자 전체가 아니라 선분 근처의 격자 칸만, 시작점부터 순서대로 찾고 첫 충돌 뒤에는 멈춥니다. 결과는 예전과 같습니다. Rapier 판에서 게임 로직이 Chipmunk 판보다 2~3배 느리던 주원인이었습니다. 좀비 6~8만 마리에서 로직 1회가 72 ms에서 29 ms로, 프레임이 113 ms에서 79 ms로 줄었습니다. 자세한 측정은 [개발 문서](docs/DEVELOPMENT.md#rapier-판에서-로직렌더가-느려짐)에 있습니다.

#### 추가
- 벤치 CSV에 공간 질의 종류별 시간·후보 수(`q_*`)와 래퍼 호출 진단(`n_angle_*` 등) 열을 더했습니다.
- `DX_RAPIER_RAYWALK`: `0`이면 예전 방식, `2`면 두 방식을 함께 돌려 차이를 셉니다.

### Rapier 래퍼 SIMD·병렬화

#### 변경
- 레이캐스트 후보 검사: 칸 순서로 복사한 경계 상자·카테고리 배열을 AVX2로 8개씩 검사합니다(AVX2가 없는 CPU나 `DX_RAPIER_RAYSIMD=0`이면 스칼라). 광선 1회가 좀비 8만 마리 이상에서 7.7 µs에서 2.8 µs로 줄었습니다. 결과는 같습니다.
- 단계 뒤 상태 읽기를 작업 스레드로 나눴습니다. 가져오기 구간이 8만 이상에서 5.6 ms에서 2.3 ms로 줄었습니다.
- 둘 다 게임 시간에는 거의 영향이 없었습니다. 좀비가 많을 때의 병목은 Rapier 단계 계산 자체입니다. ([상세](docs/DEVELOPMENT.md#질의-인덱스))

### Rapier 솔버 메모리 순서

#### 변경
- `native/rapier/rapier-solver-zorder.patch`: 솔버 몸체를 16단계마다 위치의 Morton 순서로 다시 놓고, 같은 색의 접촉을 몸체 순서로 정렬합니다. 좀비 8만 마리 이상에서 Rapier 단계가 97 ms에서 87 ms로, 솔버 계산이 26% 줄었습니다. 이 구간의 게임 시간은 프레임이 막고 있어 그대로입니다. 결과는 예전과 비트 단위로 같지 않지만 벽 밖으로 밀려나는 몸체 수는 같습니다. ([상세](docs/DEVELOPMENT.md#솔버-몸체-메모리-순서))

#### 추가
- `native/rapier/rapier-solver-stats.patch`, 벤치 `s_*` 열: 병렬 솔버의 장벽 대기와 직렬 처리 시간(`RAPIER_SOLVER_DIAG=1`).

### Rapier 직렬 구간

#### 변경
- `native/rapier/rapier-island-epoch.patch`: 게임이 매 단계 몸체 상태를 써 넣을 때마다 Rapier가 활성 집합 세대를 올려 솔버 접촉 그래프를 매 단계 처음부터 다시 만들던 것을, 소속이 바뀔 때만 올리게 했습니다(래퍼 기본 켬, `RAPIER_STABLE_EPOCH=0`으로 끔). 새 몸체는 몸체 표에 항목만 더합니다. 좀비 10만에서 그래프 유지가 6.1 ms에서 2.0 ms로 줄었습니다. 결과는 비트 단위로 같지 않지만 벽 밖으로 밀리는 몸체는 늘지 않았습니다. ([상세](docs/DEVELOPMENT.md#직렬-구간-솔버-그래프와-쌍-등록))
- `native/rapier/rapier-pair-prefetch.patch`: 넓은 범위 쌍 추가·삭제를 처리할 때 몇 이벤트 앞의 충돌체·그래프 데이터를 미리 읽습니다. 쌍 등록이 진단 기준 14.0 ms에서 9.9 ms로 줄었고 결과는 같습니다.

#### 추가
- `native/rapier/rapier-solver-stats.patch`, 벤치 `s_*` 열: 속도 조립·좁은 범위·섬 제약 수집의 하위 구간 시간, 쌍 이벤트 수, 솔버 그래프 재구성 원인(`RAPIER_SOLVER_DIAG=1`).

### 그리기와 웨이브 표시 (`mod/`)

좀비 8만 마리 이상에서 프레임이 108.0 ms에서 97.3 ms로, 게임 시간이 0.67에서 0.72로 빨라졌습니다(실제 게임, 설정마다 1회). ([상세](docs/DEVELOPMENT.md#그리기와-웨이브-표시))

#### 변경
- 웨이브 미니맵 표시(`SwarmFast.cs`, Rapier 판): 웨이브 아이콘 위치(살아 있는 유닛의 평균 칸)를 물리 DLL의 몸체 묶음에서 한 번에 계산합니다. 원작은 로직 갱신마다 유닛 목록 전체를 두 번 훑었습니다. 죽은 유닛 정리는 1초마다 원작 갱신으로 합니다(`TABPERF_SWARMFAST=0`으로 끔).
- D3D 장치 확인(`DeviceCheck.cs`): 렌더러가 스프라이트마다 부르는 `TestCooperativeLevel`을, 한 장면 안에서는 처음 성공한 결과로 대신합니다. 장면 시작·끝과 다른 스레드의 확인은 그대로 합니다(`TABPERF_DEVICECHECK=0`으로 끔).
- 루트 목록 지우기(`DrawFast.cs`): `DrawScene`이 매 프레임 빛·후처리 물체를 보이는 물체 목록(약 6.6만 개)에서 앞부터 찾아 지우던 것을, 넣었을 때만 끝부터 찾습니다(`TABPERF_FASTREMOVE=0`으로 끔).
- 단순 스프라이트(`DrawFast.cs`): 묶음·텍스처·상태를 바꿀 필요가 없는 스프라이트는 렌더러의 `DrawObject` 대신 정점 6개를 직접 묶음에 넣습니다. 스프라이트의 약 70%가 해당하고, 그리기가 프레임당 59.6 ms에서 53.8 ms로 줄었습니다. 검사 모드에서 8,160만 번 원작과 바이트 단위로 같았습니다(`TABPERF_FASTDRAW=0`으로 끔, `TABPERF_FASTDRAW_CHECK=1`로 검사).

#### 추가
- 물리 DLL 몸체 묶음(`DX_GroupNew`·`DX_GroupFree`·`DX_GroupAdd`·`DX_GroupCells`): 몸체 기록을 묶어 두고, 지워진 몸체는 DLL이 스스로 빼며, 살아 있는 몸체의 칸 합계를 한 번에 돌려줍니다.
- 웨이브 표시 검사 모드(`TABPERF_SWARMCHECK=1`): 원작 방식과 수·위치를 비교해 로그에 남깁니다.

#### 수정
- `DX_SetOption(204, -1)`(단계 카운터 확인)이 물리 단계가 끝날 때까지 기다리지 않습니다. 벤치가 1초마다 부릅니다.

### DXVK (선택)

#### 추가
- `engine.json`의 `"dxvk": true`(또는 `TABPERF_DXVK=1`)이면 모드가 시작할 때 모드 폴더의 `dxvk\d3d9.dll`([DXVK](https://github.com/doitsujin/dxvk))을 먼저 불러, 게임의 Direct3D 9가 Vulkan 위에서 돕니다. 게임 폴더는 건드리지 않습니다. `dxvkMaxFps`(`TABPERF_DXVK_FPS`)로 프레임 수를 제한합니다.
- `scripts\install.ps1 -Dxvk on|off`: DXVK 공식 릴리스 3.1.1을 받아 SHA-256을 확인하고 64비트 `d3d9.dll`을 넣거나 뺍니다.
- 벤치 `engine` 열과 로그에 DXVK 사용 여부(`+ DXVK`)를 남깁니다.
- 측정(Rapier): 6만 마리 이상에서 프레임 4~7% 단축, 게임 시간·CPU는 거의 같음. 좀비가 적을 때는 FPS와 CPU 사용량이 늘어 기본은 꺼짐입니다. ([측정](docs/DEVELOPMENT.md#dxvk))

### 로직 따라잡기 (`mod/`)

#### 변경
- 프레임이 66.7 ms보다 길면 게임 로직이 프레임 길이만큼(최대 4틱) 따라잡을 때까지 게임 시계를 세우지 않습니다. 원작은 로직이 2틱 밀리면 시계를 세워, 느린 프레임에서는 로직이 프레임당 2틱만 돌았습니다. 실제 게임(Rapier, 4회씩)에서 게임 시간이 6~8만 0.95 → 1.01, 8~10만 0.80 → 0.87, 10만 이상 0.67 → 0.74로 빨라졌습니다. `engine.json`의 `"logicMaxLag": 1`(또는 `TABPERF_LOGIC_LAG=1`)이면 원작 규칙입니다. ([상세](docs/DEVELOPMENT.md#로직-따라잡기))
- 벤치에 `l_lag` 열(그때의 허용 틱 수)을 더했습니다.
- 측정 도구 `TABPERF_TICKPROF=1|full`: 로직 틱 안의 시스템·컴포넌트 갱신을 메서드별로 재어 로그에 남깁니다. 10만 마리에서 틱 15 ms 중 시스템은 0.5 ms이고 나머지는 엔티티 갱신이며, 그 CPU 시간의 29%가 행동 트리(`CBehaviour`), 14%가 이동(`CMovable`)이었습니다. `full`은 행동 트리 잎 노드와 `CMovable` 단계까지 잽니다. ([상세](docs/DEVELOPMENT.md#로직-틱-내부))

### 모드 소스 구성

#### 변경
- `mod/`를 영역별 폴더(`Core`, `Physics`, `Logic`, `Render`, `Diag`)로 나눴습니다. 빌드는 하위 폴더의 소스를 모두 넣습니다.
- 렌더러(`DXRender_SlimDX`)의 형식·필드 이름과 정점 크기 확인을 `mod/Render/Renderer.cs` 한 곳에 모았습니다(체력바·장치 확인·그리기 빠른 길이 함께 씀). 호출 바꾸기 트랜스파일러는 `mod/Core/Patching.cs`로 옮겼습니다.
- 환경 변수로만 켜고 끄는 모드 스위치(`TABPERF_SUCS`, `TABPERF_FASTBODY`, `TABPERF_POSTPROF`, 그리기·웨이브 표시 스위치)도 다른 설정처럼 `1`/`0`, `on`/`off`, `true`/`false`, `yes`/`no`를 받고, 잘못된 값은 로그에 남깁니다.

### Rapier 지형 충돌

#### 수정
- **좀비가 강·절벽 같은 갈 수 없는 지형을 통과하던 문제**(Rapier 판): 게임은 지형을 엔티티 없는 정적 사각형으로 만들어 충돌 범주·마스크를 정하지 않습니다. Chipmunk는 새 모양이 모든 범주·마스크와 충돌하는 것이 기본인데, Rapier 래퍼는 둘 다 0으로 시작해 지형이 아무것과도 충돌하지 않았습니다. 같은 이유로 질의(레이캐스트·영역 등)에도 지형이 잡히지 않았습니다. 이제 Chipmunk와 같은 기본값으로 시작합니다. 건물은 게임이 마스크를 정하므로 원래 막혔습니다. 시험 장면 6.6만 마리에서 갈 수 없는 칸에 선 좀비가 약 2만 마리에서 0마리가 됐습니다(성능 시험 `-Terrain`).

### 성능 시험과 문서

#### 변경
- `scripts\bench.ps1`: 같은 시나리오로 원작(모드 없음)·최초 배포본(`ef035e1`)·현재 모드 Chipmunk·Rapier를 시험합니다(`-Target original,first,chipmunk,rapier`, `-Dxvk`). 정해진 시간 대신 좀비가 `-Zombies`(기본 10만, 원작은 `-OriginalZombies` 5만)마리가 되면 끝납니다. 최초 배포본은 그 커밋을 한 번 빌드해 쓰고, 게임 폴더에서 바꾼 것은 끝나면 되돌립니다. ([사용법](docs/BENCH.md))
- 측정은 별도 모드 TABBench(`bench/`, `scripts\build.ps1 -Target bench`)가 합니다. 모든 대상이 같은 측정 코드로 잽니다.
  - 측정 모드가 렌더 스레드에서 쓴 시간은 프레임 시간에서 뺍니다.
  - 좀비 수는 형식별 엔티티 목록 크기로 읽습니다. 전에는 1초마다 좀비를 하나씩 세어 그 프레임이 길어졌습니다.
  - 로직 작업을 기다린 시간, 로직 틱 수, CPU 사용량을 함께 남깁니다. `-Terrain`이면 갈 수 없는 칸에 선 좀비 수도 셉니다.
- 모드의 성능 시험 모드(`mod/Diag/Bench.cs`)는 개발용 진단 기록만 남깁니다(`bench.ps1 -Diag`). 시나리오 진행은 TABBench로 옮겼습니다.
- 로직 갱신 계측(`LogicProf`)은 진단 기록이나 `TABPERF_TICKPROF`를 켤 때만 겁니다. 전에는 평소에도 로직 작업마다 시계를 읽었습니다.
- README: 빌드 방법을 [빌드 문서](docs/BUILD.md)로 나누고, 최초 배포본 대비 주요 수정을 정리했습니다. 성능 측정은 위 네 대상의 실제 게임 시험만 남기고, 비교 도구와 DXVK 측정은 [개발 문서](docs/DEVELOPMENT.md)로 옮겼습니다.

### 알려진 문제
- 지운 몸체의 분리(separate) 콜백이 원작보다 한 단계 늦게, 다음 `Space_Step`에서 호출됩니다. 이 때문에 README의 "스위치 3·5·6·8을 끄면 원작과 비트 단위로 같다"는 설명은 몸체를 지우는 장면에서 맞지 않습니다.
  - 몸체 12,000개 장면 1에서는 위치·속도는 같고 분리 콜백만 다릅니다.
  - 스위치 4(지우기 지연)까지 끄면 원작과 같아집니다.
  - 이번 변경 전부터 있던 동작입니다.
- 현재 MSVC는 `/fp:contract-`를 모르는 옵션이라며 무시합니다(D9002).
- Rapier 판은 원작과 계산 결과가 같지 않습니다.
  - 충돌 시작 콜백의 `false`는 다음 단계부터 적용됩니다.
  - 콜백은 단계 계산이 끝난 뒤 호출됩니다.
  - 센서끼리의 겹침은 감지하지 않을 수 있습니다.
