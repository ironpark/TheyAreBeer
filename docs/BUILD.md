# 빌드

모드와 물리 엔진을 소스에서 빌드하는 방법입니다. 빌드한 결과물의 설치는 [README](../README.md#설치)를 보세요.

## 준비

- Windows x64 환경
- Visual Studio 2019/2022 또는 Visual Studio Build Tools
  - 물리 DLL: **C++를 사용한 데스크톱 개발** 워크로드 및 MSVC x64 도구
  - 엔진 모드: **MSBuild 및 Roslyn C# 컴파일러**, .NET Framework 4.7.2 이상
- Rapier 엔진(선택): [rustup](https://rustup.rs/), CMake 3.25 이상, git
- [TABModLoader](https://codeberg.org/JKstring/TABModLoader)의 참조 파일. 아래 세팅 스크립트가 내려받습니다.
  - TABModLoader가 패치한 `TheyAreBillions.exe`
  - TABModLoader 저장소의 `Lib` 폴더에 있는 `DXVision.dll`, `0Harmony.dll`

`ModInfos.json`에 명시된 요구 로더 버전은 `1.1.0`입니다. 네이티브 빌드 스크립트는 경로에 공백이 있으면 중단하므로, 저장소를 공백이 없는 경로에 두세요.

## 세팅과 빌드

저장소 루트에서 PowerShell로 실행합니다.

### 세팅 (게임 확인, 참조 파일, 모드로더 패치)

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
- `-Version <태그>`: 받을 TABModLoader 릴리스입니다. 기본은 모드가 맞춰진 `v1.1.6`이고, `latest`면 최신 릴리스를 받습니다. v1.1.7은 게임의 전력 계산 코드를 바꿔 모드가 아직 빌드되지 않습니다.
- `-RefDir <폴더>`: 참조 파일을 저장할 폴더를 지정합니다.
- `-Force`: 이미 있어도 다시 받습니다.

### 빌드

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\build.ps1
```

빌드 스크립트 하나로 물리 엔진과 엔진 모드를 모두 빌드합니다. 일부만 빌드하려면 `-Target`에 대상을 쉼표로 나열하세요(예: `-Target chipmunk,mod`). 대상은 아래 순서로 빌드됩니다.

| 대상 | 출력 파일 | 설명 |
| --- | --- | --- |
| `chipmunk` | `native\out\DXPhysicsNative.dll` | Chipmunk2D를 여러 코어로 계산하는 물리 DLL |
| `harness` | `native\out\compare.exe` | 원작 DLL과 대체본을 함께 돌려 비교하는 도구 |
| `rapier` | `native\out\rapier\DXPhysicsNative.dll`, `rapier2d_ffi.dll`, `rapier2d_ffi_avx2.dll`, `rapier2d_ffi_avx512.dll` | [Rapier](https://rapier.rs/) 2D 엔진으로 동작하는 물리 DLL |
| `mod` | `mod\out\TABPerf.dll`, `mod\out\ModInfos.json` | 엔진 모드 |
| `bench` | `bench\out\TABBench.dll`, `bench\out\ModInfos.json` | 성능 시험용 모드. [성능 시험](BENCH.md)이 시험 동안만 설치합니다 |

- `-Target`을 생략하면(`all`) 모두 빌드합니다. 이때 비교 도구 빌드가 실패하면 경고만 남기고, Rapier에 필요한 도구(git, cmake, rustup)가 없으면 Rapier를 건너뜁니다. 대상을 직접 지정하면 실패를 오류로 처리합니다.
- 엔진 모드는 기본으로 `refs\`의 참조 파일을 씁니다. 다른 폴더는 `-RefDir`로 지정하고, Roslyn 컴파일러를 자동으로 찾지 못하면 `-Csc`로 `csc.exe` 경로를 지정하세요.
- 각 단계의 컴파일 기록은 출력 폴더의 `build.log`, `build-harness.log`, `rapier-build*.log`에 남습니다. 실패하면 오류 줄을 보여 주고 멈춥니다.

Rapier 빌드는 Rapier의 공식 C 바인딩을 [공식 문서](https://rapier.rs/docs/user_guides/c/getting_started_c)의 CMake 빌드로 만들며, 다음 순서로 진행합니다.

1. Rapier 저장소를 고정 커밋으로 `native\out\rapier-src`에 받습니다.
2. `native\rapier\*.patch`를 받은 소스에 적용합니다. 패치 목록이나 내용이 바뀌면 소스를 고정 커밋 상태로 되돌린 뒤 모두 다시 적용합니다.
   - `rapier-c-batch-write.patch`: 여러 몸체의 위치·속도를 한 번의 호출로 쓰는 `r2RigidBody_WriteStates`를 C 바인딩에 더합니다(공식 바인딩은 몸체·값마다 호출해야 하고, 호출마다 세계 잠금과 오류 처리를 거칩니다).
   - `rapier-island-epoch.patch`: 게임이 몸체 상태를 써 넣을 때마다 Rapier가 솔버 접촉 그래프를 처음부터 다시 만들지 않게 합니다. 활성 집합의 소속이 바뀔 때만 다시 만듭니다(래퍼 기본 켬, `RAPIER_STABLE_EPOCH=0`으로 끔).
   - `rapier-pair-prefetch.patch`: 넓은 범위의 쌍 추가·삭제를 처리할 때 몇 개 앞 이벤트의 데이터를 미리 읽습니다.
   - `rapier-solver-park.patch`: 병렬 솔버의 작업 스레드가 단계를 기다릴 때 `RAPIER_SYNC_SPIN`번 돈 뒤 잠들게 합니다(래퍼 기본 2000). 게임의 로직·그리기 스레드에 코어를 돌려줍니다.
   - `rapier-solver-stats.patch`: 병렬 솔버의 장벽 대기·넘침 처리 시간과 색 수, 단계 안 직렬 구간 시간을 셉니다(`RAPIER_SOLVER_DIAG`가 있을 때만).
   - `rapier-solver-zorder.patch`: 솔버 몸체를 위치의 Morton 순서로(16단계마다), 색 안의 접촉을 몸체 순서로 정렬해 캐시 적중을 높입니다(래퍼 기본 켬, `RAPIER_BODY_SORT=0`·`RAPIER_CONTACT_SORT=0`으로 끔).
   - `rapier-c-step-counters.patch`: 마지막 단계의 Rapier 내부 구간 시간(넓은 범위·좁은 범위 충돌 검사, 섬 제약 수집, 솔버 등)과 접촉 수를 한 번에 읽는 `r2StepCounters`를 더합니다. Rapier의 `profiler` 기능으로 빌드하며, 카운터는 켤 때만 잽니다.
3. 필요한 Rust 버전(1.99.1)이 없으면 설치합니다.
4. `-DRAPIER_DIMENSION=2 -DRAPIER_PRECISION=32 -DRAPIER_ENABLE_PARALLEL=ON -DRAPIER_SIMD_LANES=8 -DRAPIER_FEATURES=profiler`로 세 번 빌드하고 CMake 통합 테스트를 돌립니다. 모든 x64 CPU용, AVX2·FMA용(Rust `+avx2,+fma`), AVX-512용(여기에 `+avx512f,+avx512vl,+avx512bw,+avx512dq,+avx512cd`)이며 모두 8레인 솔버입니다. Rust 쪽은 fat LTO·코드 생성 단위 1개로 빌드합니다(Cargo 환경 변수 `CARGO_PROFILE_RELEASE_LTO=fat`). 빌드 PC가 그 명령어를 지원하지 않으면 그 빌드의 테스트는 건너뜁니다.
5. 같은 정의(`RAPIER_DIM2`, `RAPIER_F32`, `RAPIER_PARALLEL`)로 `DXPhysicsNative.dll`을 빌드합니다. 이 DLL은 CPU와 OS가 지원하는 가장 높은 빌드(AVX-512 → AVX2 → 기본)를 같은 폴더에서 불러옵니다. 파일이 없으면 다음 단계로 내려갑니다.

## 배포 파일

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\package.ps1
```

모두 빌드한 뒤 실행하면 `dist\TABPerf-v<버전>.zip`과 `dist\ModPackage.zip`을 만듭니다. `TABPerf-v<버전>.zip`에는 `TABPerf` 폴더 하나가 들어 있고, `ModPackage.zip`은 같은 파일을 폴더 없이 담습니다(TABModLoader 자동 업데이트가 받아 모드 폴더에 그대로 풉니다). 설치 스크립트가 게임에 넣는 구성과 같으며, 여기에 DXVK, `README.txt`, 라이선스 문서(`licenses\`: 이 패치 MIT, Chipmunk2D MIT, Rapier Apache-2.0, DXVK zlib)가 더해집니다.

GitHub Actions(`.github/workflows/build.yml`)는 Windows에서 세팅(`-NoPatch`), 전체 빌드, 패키지를 돌리고 압축 파일을 아티팩트로 남깁니다. `v<버전>` 태그를 푸시하면 실행되어 두 압축 파일로 GitHub 릴리스를 만듭니다. 손으로도 돌릴 수 있습니다(Actions 탭의 Run workflow). `main` 푸시에는 Rapier 빌드 입력(`native/rapier/`, `scripts/build.ps1`)이 바뀔 때만 돌아, 태그 실행이 쓸 Rapier 캐시를 미리 만듭니다(태그 실행은 `main`의 캐시만 가져올 수 있음). 문서만 바꾼 푸시에는 돌지 않습니다.

빌드 시간은 대부분 Rapier입니다. CPU 수준별 세 빌드는 동시에 돌고, 각 빌드는 `native\out\rapier-sdk*\tabperf-build.txt`에 빌드 입력(Rapier 커밋, Rust 버전, CMake 옵션, 패치 해시)을 남깁니다. 입력이 같으면 다음 빌드는 소스를 받거나 Rust를 돌리지 않고 그대로 씁니다. CI는 이 폴더와 TABModLoader 참조 파일, DXVK를 캐시합니다.

### 버전

버전은 `mod\ModInfos.json`의 `Version` 하나입니다. 빌드 스크립트가 이 값을 `TABPerf.dll`에 넣고(`ModBuildInfo.Version`, 로그 첫 줄), 패키지 이름에 쓰고, 모드로더 자동 업데이트가 릴리스 태그와 비교합니다. 태그 빌드는 먼저 `scripts\set-version.ps1 -Version <태그>`로 이 값을 태그에 맞춥니다. 저장소의 값도 같이 맞추려면 릴리스 전에 다음을 실행해 커밋합니다.

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\set-version.ps1 -Version 0.0.2
git commit -am "version 0.0.2"; git tag v0.0.2; git push origin main v0.0.2
```

### 모드로더 패딩

물리 DLL을 수정할 때는 `native/dxphysics/dxdiag.c`의 `dx_text_pad`를 유지해야 합니다. TABModLoader가 DLL 시작 주소의 `+0x13B3` 위치에 5바이트를 덮어쓰기 때문에, 해당 공간에 실제 코드가 배치되면 게임이 종료될 수 있습니다. 빌드 스크립트는 두 물리 DLL 모두 링크 맵에서 이 위치를 검사하고, 어긋나면 빌드를 실패시킵니다. Rapier 판은 같은 이유로 Rust 코드를 `DXPhysicsNative.dll`에 정적으로 넣지 않고, 별도의 `rapier2d_ffi.dll`에서 지연 로드합니다. Rust 코드는 일반 `.text` 섹션에 들어가 패딩보다 앞에 놓이기 때문입니다.
## 비교 도구

비교 도구의 기본 사용법은 다음과 같습니다.

```powershell
.\native\out\compare.exe "C:\Original\DXPhysicsNative.dll" ".\native\out\DXPhysicsNative.dll"
```

추가 인자는 `[장면|all] [몸체 수] [단계 수]`입니다. 원본 소스 안내에 따르면 병렬 옵션 `3`, `5`, `6`, `8`을 끈 조건에서 원작 DLL과 위치·속도의 비트 단위 일치를 확인했습니다. 다만 몸체를 지우는 장면에서는 지운 몸체의 분리 콜백이 한 단계 늦어, 옵션 `4`(지우기 지연)까지 꺼야 같아집니다. 기본 병렬 설정 전체에 대한 동일성 보장을 뜻하지는 않습니다.

DLL 하나만 재는 모드도 있습니다. `compare.exe prof <DLL> [몸체 수] [단계 수] [반복]`은 벽으로 둘러싼 기지로 몰리는 장면의 단계 시간을, `compare.exe ray <DLL> [좀비 수] [단계 수] [광선 수]`는 레이캐스트 시간을 잽니다.
