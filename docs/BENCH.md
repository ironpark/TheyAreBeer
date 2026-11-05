# 성능 시험

게임 안 자동 성능 시험(`scripts\bench.ps1`)의 사용법입니다. README의 [성능 측정](../README.md#성능-측정)이 이 시험으로 잰 결과입니다. 원인 분석과 실험은 [DEVELOPMENT.md](DEVELOPMENT.md)에 있습니다.

## 시험 내용

모든 대상을 같은 시나리오로 돌리고, 같은 측정 코드로 잽니다.

1. 시작 화면에서 점수 배율 900% 생존 게임을 시작합니다.
   - 테마 「부식의 땅」(점수 배율이 가장 높은 테마), 감염체 최다(150%), 80일입니다.
   - 맵 시드가 고정(`20261104`)이라 대상마다 같은 맵입니다.
2. 시작 애니메이션이 끝나 입력이 켜지면 카메라를 게임의 최대 축소 폭(마우스 휠로 뺄 수 있는 끝)으로 맞추고, 시험 내내 유지합니다(`-Zoom max`).
3. 레벨 시작 5초 뒤 피해를 끄고(`CLife.DamageAvailable`, 게임의 디버그 스위치) 마지막 웨이브를 4번 발생시킵니다. 생성기 4곳에서 한 번에 생성기당 약 4,350마리가 나옵니다.
4. 좀비가 죽지 않고 지휘본부도 무너지지 않으므로, 좀비가 계속 늘며 지휘본부로 몰립니다.
5. 1초마다 CSV에 한 줄을 씁니다. 살아 있는 좀비가 10만 마리(`-Zombies`, 원작은 5만 마리 `-OriginalZombies`)가 되면 게임을 끕니다.

게임 설정(그래픽 옵션, 빠른 타이머 FastTimming, 수직 동기화 등)은 사용자의 게임 설정을 그대로 씁니다. 쓰인 값은 `%APPDATA%\TABPerf\tabbench.log`에 남습니다.

### 시험 대상

| `-Target` | 대상 | 게임 폴더에서 바꾸는 것 (시험이 끝나면 되돌림) |
| --- | --- | --- |
| `original` | 최적화 모드 없는 원작 게임 | `Mods\TABPerf`를 `Mods` 밖으로 잠시 옮김 |
| `first` | 모드 최초 배포본(커밋 `ef035e1`): 엔진 모드와 Chipmunk 다중 코어 물리 DLL | `Mods\TABPerf`를 최초 배포본으로 바꿈. 당시 설치 방식대로 게임 폴더의 `DXPhysicsNative.dll`도 최초 배포본 DLL로 바꿈 |
| `chipmunk` | 현재 모드, Chipmunk 다중 코어 엔진 | 없음(설치된 `Mods\TABPerf`) |
| `rapier` | 현재 모드, Rapier 엔진 | 없음 |

- `-Dxvk`를 주면 `chipmunk`·`rapier`를 DXVK로 돌립니다. 원작과 최초 배포본은 DXVK를 불러올 수 없어 늘 시스템 Direct3D 9로 돕니다.
- 최초 배포본은 처음 시험할 때 한 번 빌드합니다. git으로 그 커밋을 꺼내 당시의 빌드 스크립트로 빌드하고, 결과를 `native\out\first-release`에 둡니다.
- 원작에서도 시험 시나리오를 돌리려면 TABModLoader와 아래 측정 모드가 있어야 합니다. 로더 자체는 게임 처리에 끼어들지 않습니다.

### 측정 모드 (TABBench)

측정은 TABPerf가 아니라 별도 모드 `bench\TABBench.cs`(`Mods\TABBench`)가 합니다.
- 원작·최초 배포본·현재 모드가 같은 측정 코드로 잽니다.
- 현재 모드의 진단 계측이 결과에 섞이지 않습니다.
- 스크립트가 시험하는 동안만 설치합니다. `TABBENCH_CSV` 환경 변수가 없으면 아무것도 하지 않습니다.

측정 기준은 다음과 같습니다.

- **프레임 시간**: 렌더 스레드에서 한 프레임 시작(`ZXSystem_GameLevel.OnStartFrame`)부터 다음 프레임 시작까지입니다. 화면에 보이는 프레임 간격과 같습니다.
  - 게임은 프레임마다 장면을 그리기 전에, 이전 프레임이 띄운 로직 작업(그사이 밀린 로직 틱)이 끝나기를 기다립니다(`DXGame.WaitForGameLogic`). 로직 작업이 그리기보다 오래 걸리면 그 차이가 프레임 시간에 들어갑니다.
  - 이 기다림은 따로 재어 `logic_wait_ms`로 남깁니다. 로직 작업 본문이 끝난 시각이 기다림이 시작되는 지점(레벨 시스템의 `OnSetCamera` 직후)보다 얼마나 늦었는지를 잽니다. 그 전에 끝났으면 0입니다.
  - `WaitForGameLogic` 자체에는 훅을 걸지 않습니다. 몇 바이트짜리 메서드라 JIT가 호출하는 쪽에 인라인해, 훅이 불리지 않습니다(처음 버전에서 확인). 로직 작업 본문은 대리자로 불려 인라인되지 않으며, 초당 작업 수(`logic_tasks`)가 프레임 수와 같은지로 훅이 불리는 것을 확인합니다.
  - 이 시험에서는 원작과 현재 모드 모두 이 기다림이 거의 0입니다. 로직 틱이 그리기보다 먼저 끝나기 때문입니다. 따라서 프레임 시간은 렌더 스레드 자신의 일(장면 갱신, 그리기, 화면 표시)입니다.
- **측정 모드 자신의 시간은 빼고 잽니다.**
  - 측정 모드가 렌더 스레드에서 쓰는 시간(1초마다 좀비 수 읽기, CSV 쓰기 등)을 매번 재어 그 프레임 시간에서 뺍니다. 그 합계는 `bench_ms` 열에 남습니다(초당 약 0.4 ms).
  - 프레임마다 측정 모드가 하는 일은 시계를 몇 번 읽는 것이 전부입니다. 로직 스레드에서는 로직 작업이 끝날 때 시계를 한 번 읽고, 물리 스레드에는 아무것도 걸지 않습니다.
  - 좀비 수는 게임의 형식별 엔티티 목록 크기를 읽습니다(`CountEntitiesOfType<Zombie>`). 좀비를 하나씩 세지 않습니다.
- **게임 진행 속도**: 실제 1초 동안 흐른 게임 시계(`DXGame.GameTime`)입니다. 1.0이면 제 속도입니다.
- **CPU 사용량**: 그 1초 동안 프로세스 전체가 쓴 CPU 시간을 코어 수로 나타냅니다.

물리 단계 시간은 기록하지 않습니다. 대상마다 물리 엔진과 게임 시계 방식이 달라 같은 기준으로 잴 수 없기 때문입니다. 물리 단계의 영향은 게임 진행 속도에 드러납니다. 엔진 안쪽을 보려면 아래 [진단 기록](#진단-기록--diag)을 씁니다.

## 준비

- `scripts\build.ps1 -Target mod,bench`와 시험할 엔진(`chipmunk`, `rapier`)을 빌드하고, `scripts\install.ps1`로 설치합니다.
- 최초 배포본을 시험하려면 `refs\`의 참조 파일(`scripts\setup.ps1`)과 MSVC가 있어야 합니다.
- `-Dxvk`에 쓸 DXVK는 `scripts\install.ps1`이 받아 둡니다. 모드 폴더에 없으면 시험하는 동안만 넣습니다.
- Steam이 켜져 있어야 합니다.
- 게임이 꺼져 있어야 합니다.

스크립트가 게임 폴더와 설정을 다루는 방식은 다음과 같습니다.
- **`steam_appid.txt`:** 게임은 보통 Steam을 거쳐 다시 시작되는데, 이때 스크립트가 넘긴 환경 변수가 사라집니다. 그래서 실행하는 동안만 게임 폴더에 `steam_appid.txt`를 만들어 게임이 바로 실행되게 하고, 끝나면 지웁니다. 원래 있던 `steam_appid.txt`는 그대로 둡니다.
- **`engine.json`:** 설정은 환경 변수로만 넘기므로 바꾸지 않습니다.
- **되돌리기:** 게임 폴더에서 바꾼 것은 시험이 실패해도 마지막에 모두 되돌립니다. 게임 폴더 물리 DLL의 해시가 처음과 같은지도 확인합니다.

## 실행

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\bench.ps1 -Dxvk
```

대상마다 게임을 한 번씩 띄워 시험하고, 끝나면 결과를 표로 보여 줍니다. 10만 마리까지 현재 모드는 레벨 로딩을 포함해 몇 분이 걸리고, 원작은 게임이 느려 수십 분이 걸립니다.

| 인자 | 기본값 | 뜻 |
| --- | --- | --- |
| `-Target` | `original,first,chipmunk,rapier` | 시험할 대상(위 표)을 쉼표로 나열 |
| `-Dxvk` | 끔 | `chipmunk`·`rapier`를 DXVK로(없으면 시스템 Direct3D 9). `-Renderer dxvk|d3d9`로 직접 골라도 됨 |
| `-Zombies` | `100000` | 이 수가 되면 시험 끝 |
| `-OriginalZombies` | `50000` | 원작(`original`)은 이 수에서 끝. 좀비가 많으면 원작이 매우 느려지기 때문 |
| `-Timeout` | `3600` | 레벨 시작부터 이 시간(초)이 지나면 좀비 수와 상관없이 끝 |
| `-Runs` | `1` | 대상마다 반복할 횟수 |
| `-Factor` | `4` | 마지막 웨이브를 몇 번 발생시킬지(1~20) |
| `-Delay` | `5` | 레벨 시작부터 웨이브 발생까지(초) |
| `-Seed` | `20261104` | 생존 게임 맵 시드 |
| `-Zoom` | `max` | `max`는 가장 넓은 화면으로 고정, `off`는 카메라를 건드리지 않음 |
| `-Terrain` | 끔 | 1초마다 갈 수 없는 지형 칸(강·절벽 등)에 선 좀비 수를 셈(`zombies_in_terrain`). 모든 좀비를 로직 잠금 안에서 훑으므로 시간 비교용 실행에는 쓰지 않음 |
| `-Diag` | 끔 | 현재 모드의 진단 기록도 남김(아래) |
| `-Threads` | `-1` (자동) | `-Diag`일 때 물리 작업 스레드 수(`DX_SetOption(0)`), 레벨 시작 때 적용 |
| `-Counters` | 끔 | `-Diag`일 때 Rapier 내부 구간 시간(`rs_*` 열)도 잼(`DX_RAPIER_COUNTERS=1`). 단계가 약 7% 길어짐 |
| `-OutDir` | `bench-out\<날짜-시각>` | 결과 폴더 |
| `-GameDir` | 자동으로 찾음 | 게임 설치 폴더 |

### 자주 쓰는 실행

```powershell
# README의 측정: 다섯 대상, 현재 모드는 DXVK
.\scripts\bench.ps1 -Target original,first,chipmunk,rapier,chipmunk-vulkan -Dxvk

# 현재 모드 두 엔진만, 편차를 보려고 3번
.\scripts\bench.ps1 -Target chipmunk,rapier -Runs 3

# 렌더링 부담을 빼고 비교 (카메라 그대로)
.\scripts\bench.ps1 -Target chipmunk,rapier -Zoom off

# Rapier 내부까지 들여다보기 (진단 기록, 비교용 아님)
.\scripts\bench.ps1 -Target rapier -Diag -Counters -Zombies 80000
```

스크립트에 인자가 없는 설정은 환경 변수를 먼저 정한 뒤 실행합니다. 게임은 스크립트의 환경을 물려받습니다.

```powershell
# Rapier 하위 단계를 10번으로 고정
$env:DX_RAPIER_ITERATIONS = '10'
.\scripts\bench.ps1 -Target rapier -OutDir bench-out\fixed10
Remove-Item Env:DX_RAPIER_ITERATIONS
```

## 결과 파일

`-OutDir` 폴더에 남습니다. 그래프는 `python scripts\charts.py --game <결과 폴더>`로 `docs\charts\`에 다시 그립니다.

`<대상>-<회차>.csv`에는 1초마다 한 줄이 남습니다.

| 열 | 뜻 |
| --- | --- |
| `t_s` | 레벨 시작부터 실제 경과 시간(초) |
| `frames`, `frame_ms`, `frame_max_ms` | 그 1초의 프레임 수, 평균·최대 프레임 시간(측정 모드 시간 뺌) |
| `logic_wait_ms` | 프레임마다 로직 작업을 기다린 평균 시간 |
| `logic_tasks`, `logic_ticks` | 그 1초에 끝난 로직 작업 수(훅 확인용, 보통 프레임 수와 같음), 처리한 로직 틱 수(`DXGame.IAIteration`, 제 속도면 30) |
| `zombies` | 살아 있는 좀비 수 |
| `zombies_in_terrain` | `-Terrain`일 때 갈 수 없는 칸(지도의 `CAccesibleLayer`)에 선 좀비 수, 아니면 -1 |
| `game_time_s`, `game_speed` | 게임 시계(초), 그 1초 동안의 게임 진행 속도 |
| `cpu_cores` | 프로세스 전체 CPU 사용량(코어 수) |
| `bench_ms` | 측정 모드가 그 1초 동안 렌더 스레드에서 쓴 시간(프레임 시간에서 이미 뺌) |
| `paused`, `note` | 일시 정지 여부와 표시(`swarm x4` 웨이브 발생, `end` 목표 도달, `timeout` 시간 초과) |

`summary.csv`에는 실행마다 한 줄이 남습니다.

| 열 | 뜻 |
| --- | --- |
| `real_s`, `game_s` | 웨이브 발생부터 끝까지 걸린 실제 시간과 게임 시간 |
| `zombies_max` | 최대 좀비 수 |
| `speed_90k`, `frame_ms_90k`, `logic_wait_ms_90k`, `cpu_cores_90k` | 9만 마리 이상 구간의 평균 |
| `bench_ms_per_s` | 측정 모드가 쓴 시간 |

측정 모드의 로그는 `%APPDATA%\TABPerf\tabbench.log`, 현재 모드의 로그는 `billions.log`에 남습니다.

## 결과 읽는 법

- **좀비 수로 맞춰 비교합니다.** 좀비는 게임 시간에 따라 생기므로, 게임이 느린 대상은 같은 실제 시간에 좀비가 적습니다. 그래프와 표는 좀비 수 구간으로 묶어 평균합니다.
- **게임 진행 속도가 가장 중요한 지표입니다.** 1.0이면 제 속도이고, 0.5면 게임 안의 시간이 절반 속도로 흐릅니다.
- **프레임 시간은 게임 진행 속도와 함께 봅니다.**
  - 게임이 느린 대상은 같은 좀비 수에서 프레임이 짧게 나올 수 있습니다. 같은 시간 동안 로직 틱(`logic_ticks`)과 물리 단계가 적어, 그리기와 함께 도는 다른 스레드의 일이 적고 CPU에 여유가 남기 때문입니다(`cpu_cores`).
  - 그래서 프레임이 짧다고 그 대상이 빠른 것은 아닙니다. 게임이 그만큼 덜 진행되고 있을 수 있습니다.
- **한 번 실행은 몇 % 흔들립니다.** 같은 조건으로 두 번 돌렸을 때 약 2.5% 차이가 났습니다. 이보다 작은 차이는 `-Runs`로 여러 번 돌려 확인합니다.
- **`-Zoom max`는 렌더링 부담이 큽니다.** 화면에 좀비를 모두 그리므로 프레임 시간이 늘어납니다. 물리만 비교하려면 `-Zoom off`가 낫습니다.

## 진단 기록 (`-Diag`)

현재 모드의 `mod/Diag/Bench.cs`는 개발용 진단 기록입니다. 켜면 1초마다 물리 단계·래퍼 구간·Rapier 내부·게임 쪽 처리·질의를 `<대상>-diag-<회차>.csv`에 남깁니다.

진단 기록은 다음 계측도 함께 켭니다. 그래서 이때의 측정 모드 CSV는 다른 실행과 비교하지 않습니다.
- 질의·호출마다 시계를 읽는 래퍼 진단(`DX_SetOption(356, 1)`)
- 로직 갱신 계측(`LogicProf`)

스크립트 없이 게임을 직접 실행할 때는 `%APPDATA%\TABPerf\engine.json`에 `"bench": true`를 넣습니다.
- 같은 기록이 `%APPDATA%\TABPerf\bench.csv`에 남습니다. 경로는 `benchCsv`, 물리 작업 스레드 수는 `benchThreads`로 정합니다.
- 환경 변수 `TABPERF_BENCH`, `TABPERF_BENCH_CSV`, `TABPERF_BENCH_THREADS`가 `engine.json`보다 우선합니다.
- 시나리오(게임 시작·웨이브·종료)는 측정 모드가 맡습니다. 직접 하려면 TABBench를 `Mods\TABBench`에 넣고 `TABBENCH_CSV`와 `TABBENCH_AUTO=0`을 정합니다. 게임 중 Ctrl+Shift+F9로 마지막 웨이브가 발생하고, 1초마다 기록이 남습니다.

### 진단 기록 CSV 열

`r_*`, `rs_*`, `n_*`, `s_*`, `q_*`는 Rapier 판에서만 값이 있습니다. 시간은 따로 적지 않으면 그 1초 동안의 단계당 평균입니다.

#### 기본

| 열 | 뜻 |
| --- | --- |
| `time` | 시각 |
| `engine` | 쓰인 물리 DLL |
| `paused` | 1이면 일시 정지 |
| `frames`, `frame_avg_ms`, `frame_max_ms` | 그 1초의 프레임 수, 평균·최대 프레임 시간 |
| `phys_steps`, `step_avg_ms`, `step_max_ms` | 물리 단계 수, `Space_Step` 한 번의 평균·최대 시간 |
| `zombies` | 좀비 수 |
| `note` | 표시 줄(`end` 끝) |

#### 래퍼 구간 (`r_*`)

| 열 | 뜻 |
| --- | --- |
| `r_push_ms` | 넣기(게임 값을 Rapier로) |
| `r_step_ms` | Rapier 단계(`r2Step`) |
| `r_pull_ms` | 가져오기(Rapier 값을 게임으로) |
| `r_pairs_ms` | 충돌 시작·분리 콜백 처리 |
| `r_index_ms` | 질의 인덱스 만들기 |
| `r_substeps` | 단계당 하위 단계 수(자동 조절 확인용) |
| `r_contact_pairs` | 단계당 접촉 쌍 수 |

#### Rapier 내부 구간 (`rs_*`, `-Counters`일 때)

`rs_total_ms`(단계 전체), `rs_user_ms`(사용자 변경), `rs_update_ms`, `rs_bp_ms`·`rs_bp_final_ms`(넓은 범위 충돌 검사), `rs_np_ms`(좁은 범위 충돌 검사), `rs_island_ms`(섬 구성), `rs_collect_ms`(섬 제약 수집), `rs_solver_ms`(솔버 전체), `rs_vassembly_ms`(속도 조립), `rs_vsolve_ms`(속도 풀이), `rs_vupdate_ms`, `rs_writeback_ms`, `rs_ccd_ms`(연속 충돌 검사), `rs_pairs`(접촉 쌍), `rs_contacts`(접촉 수).

#### 게임 쪽 시간 (`g_*`)

| 열 | 뜻 |
| --- | --- |
| `g_postmethods_ms` | 물리 스레드가 실행한 대기 작업(몸체 생성 등) |
| `g_pre_ms` | 단계 앞 몸체 처리(`StartUpdatePhysics`) |
| `g_post_ms` | 단계 뒤 위치 되읽기(`OnNewPositionComputed`) |
| `g_syncwait_ms` | 물리 스레드가 `DXPhysicWorld.Sync`를 기다린 시간 |
| `g_logicwait_ms` | 물리 스레드가 `DXGame.SyncLogic`을 기다린 시간 |
| `g_ia_per_s` | 그 1초의 게임 로직 갱신 수(게임 목표는 30) |
| `g_ia_ms` | 마지막 로직 갱신 시간 |
| `g_update_ms` | 물리 갱신 한 번 전체(대기 작업부터 되읽기까지) |
| `g_speed` | 게임 속도 설정 |
| `g_phys_usage`, `g_ia_usage` | 게임의 물리·로직 사용률. 1을 넘으면 게임이 시계를 그 역수만큼 늦춤 |
| `g_clock_rate` | 실제 1초 동안 흐른 게임 시간(초). 1.0이면 정상 속도 |
| `g_cpu_cores` | 그 1초 동안 프로세스 전체가 쓴 CPU(코어 수) |

#### 몸체 생성 (`n_*`)

| 열 | 뜻 |
| --- | --- |
| `n_create_per_s`, `n_create_us` | 그 1초의 몸체 생성 수, 한 번의 래퍼 안 시간(µs) |
| `n_settype_per_s`, `n_settype_us` | 몸체 타입 변경 수, 한 번의 시간(µs) |
| `n_insert_us`, `n_collider_us` | 생성 한 번의 Rapier 몸체 삽입·충돌체 삽입 시간(µs) |
| `n_indexwait_ms` | 그 1초 동안 질의 인덱스 락을 기다린 시간(ms) |
| `n_angle_per_s`, `n_angle_ms`, `n_size_*`, `n_dispose_*`, `n_sensor_*` | `Body_SetAngle`·`SetSize`·`Dispose`·`AddSensor` 호출 수와 합계 시간(ms, 단계가 잡은 월드 락 대기 포함) |
| `n_pending` | 인덱스를 다시 만들 때 대기 목록에 있던 충돌체 수(평균) |
| `n_ray_diff`, `n_ray_tie` | `DX_RAPIER_RAYWALK=2`일 때 광선 걷기와 상자 걷기의 첫 충돌이 다른 거리에서 / 같은 거리에서 다른 몸체로 갈린 수 |
| `n_catset` | 몸체 카테고리가 바뀐 수 |

#### 병렬 솔버 (`s_*`)

`RAPIER_SOLVER_DIAG=1`로 돌릴 때만 값이 있습니다(Rapier 판, `rapier-solver-stats.patch`가 들어간 Rapier 라이브러리).

| 열 | 뜻 |
| --- | --- |
| `s_wait_ms`, `s_run_ms` | 그 1초 동안 모든 작업 스레드가 장벽에서 기다린 시간, 솔버를 돈 시간 합계(ms) |
| `s_waits` | 장벽을 지난 수(작업 스레드마다 셈) |
| `s_overflow_ms` | 작업 스레드 0이 혼자 푼 넘침 제약 시간(ms) |
| `s_colors`, `s_overflow_chunks`, `s_chunks`, `s_workers` | 풀이 한 번의 색 수, 넘침 묶음 수, 묶음 수, 작업 스레드 수(평균) |

아래는 모두 단계(풀이) 하나 기준 평균이고, 시간은 ms입니다. 구간마다 시계를 읽어 그 자체로 시간이 조금 늘어납니다(쌍 등록 구간은 약 1.9배).

| 열 | 뜻 |
| --- | --- |
| `s_va_prep_ms`, `s_va_joints_ms`, `s_va_layout_ms`, `s_va_buffers_ms`, `s_va_stats_ms` | 속도 조립: 버퍼·솔버 몸체, 관절, 묶음 배치(접촉 정렬 포함), 제약 버퍼·일반 제약, 카운터 통계(Rapier 카운터를 켰을 때만) |
| `s_np_user_ms`, `s_np_register_ms`, `s_np_candidates_ms`, `s_np_update_ms`, `s_np_transitions_ms`, `s_np_intersections_ms` | 좁은 범위: 사용자 변경, 쌍 등록, 깨어 있는 몸체 표시·갱신 후보, 쌍 갱신(병렬), 접촉 시작·끝 반영, 센서 겹침 |
| `s_col_graph_ms`, `s_col_joints_ms` | 섬 제약 수집: 솔버 접촉 그래프 유지, 관절 |
| `s_bp_adds`, `s_bp_removes` | 넓은 범위가 낸 쌍 추가·삭제 이벤트 수 |
| `s_graph_rebuilds`, `s_graph_dirty`, `s_graph_bodies_ms`, `s_graph_buckets_ms` | 솔버 그래프 전체 재구성 횟수, 이번 단계에 바뀐 쌍 수, 몸체 표, 버킷 갱신 |
| `s_graph_invalid`, `s_epoch_updated`, `s_epoch_removed`, `s_epoch_slept`, `s_epoch_woken`, `s_epoch_groups` | 재구성 원인: 그래프 무효화, 세대를 올린 몸체 갱신·몸체 제거·잠들기·깨어나기·하위 단계 그룹 |
| `s_upd_fixed`, `s_upd_sleep`, `s_upd_type`, `s_upd_new`, `s_upd_shape`, `s_upd_all` | 몸체 갱신 종류(겹쳐 셈): 고정 몸체, 잠들기 표시, 타입·켜기 변경, 새 몸체, 충돌체·질량·우선도 변경, 전체 |
| `s_add_ms`, `s_add_index_ms`, `s_add_find_ms`, `s_add_new_ms`, `s_add_pooled` | 쌍 추가 전체, 그래프 인덱스, 노드·간선 찾기, 쌍 만들기, 재사용 풀에서 꺼낸 수 |
| `s_rm_lookup_ms`, `s_rm_unlink_ms`, `s_rm_graph_ms`, `s_rm_swap_ms`, `s_rm_moved_ms`, `s_rm_islands_ms`, `s_rm_rest_ms` | 쌍 삭제: 조회, 간선 찾기·연결 해제, 솔버 그래프·힘 이벤트 목록, 간선 배열 swap-remove, 옮겨진 간선 고치기, 섬 연결, 색·깨우기·재사용 |

#### 공간 질의 (`q_*`)

종류(`area` 영역, `radius` 반경, `fast` 반경 배열, `near` 가장 가까운 몸체, `ray` 레이캐스트, `space` 남은 공간)마다 그 1초 기준입니다(Rapier 판만). 이 열과 `n_angle_*`~`n_sensor_*`는 질의·호출마다 시계를 읽어서 평소에는 꺼져 있고, 진단 기록을 켤 때만 켜집니다(`DX_SetOption(356, 1)`).

| 열 | 뜻 |
| --- | --- |
| `q_<종류>_per_s`, `q_<종류>_ms` | 호출 수, 합계 시간(ms, 모든 스레드 합) |
| `q_<종류>_lock_ms`, `q_<종류>_cb_ms` | 그중 인덱스 락 대기, 게임 콜백 시간(ms) |
| `q_<종류>_cand`, `q_<종류>_hits` | 한 번에 살펴본 인덱스 항목 수, 상자·마스크를 통과한 수 |

#### 로직 갱신 (`l_*`)

렌더 스레드는 프레임마다 `DXGame.UpdateGameLogic`을 불러, 이전 로직 작업을 기다린 뒤 밀린 33.3 ms 틱을 처리할 작업을 띄웁니다. 모두 그 1초 기준입니다.

| 열 | 뜻 |
| --- | --- |
| `l_calls`, `l_wait_ms` | `UpdateGameLogic` 호출 수, 이전 작업을 기다린 평균 시간 |
| `l_tasks`, `l_start_ms`, `l_task_ms` | 로직 작업 수, 시작 지연 평균, 실행 시간 평균 |
| `l_ticks`, `l_idle` | 처리한 로직 틱 수, 틱 없이 끝난 작업 수 |
| `l_block_ia_ms`, `l_block_phys_ms`, `l_run_ms` | 게임 시계 루프가 로직 지연·물리 지연으로 멈춘 시간과 흐른 시간(ms). 모드의 빠른 타이머 루프에서만 값이 있음 |
| `l_lag` | 그 순간 게임 시계가 로직보다 앞설 수 있는 틱 수(1이 원작, 프레임이 길면 `logicMaxLag`까지) |

## 실험용 환경 변수

`bench.ps1`에 인자가 없는 설정입니다(주로 `-Diag`와 함께 씀). 실행 전에 정하면 게임에 넘어갑니다. 켜기/끄기 값은 모드 설정(`TABPERF_*`)은 `1`/`0`, `on`/`off`, `true`/`false`, `yes`/`no`를, 물리 DLL 설정(`DX_*`)은 `1`/`0`을 받습니다.

| 변수 | 뜻 |
| --- | --- |
| `DX_PHYS_THREADS` | 물리 작업 스레드 수(`0`은 작업 스레드 끔). `-Diag -Threads`가 있으면 그쪽이 우선 |
| `DX_RAPIER_ITERATIONS` | Rapier 하위 단계 수 고정(1~30). 자동 조절도 꺼짐 |
| `DX_RAPIER_ADAPT` | `0`이면 하위 단계 자동 조절 끔(게임 값 고정) |
| `RAPIER_SYNC_SPIN` | Rapier 솔버 작업 스레드가 단계를 기다릴 때 잠들기 전 spin 횟수(기본 2000, `0`이면 잠들지 않음) |
| `DX_RAPIER_ADAPT_MIN`, `DX_RAPIER_ADAPT_MAX` | 자동 조절 범위(기본 5~20) |
| `DX_RAPIER_COUNTERS` | `1`이면 내부 구간 시간(`-Counters`와 같음) |
| `DX_RAPIER_CCD` | `0`이면 연속 충돌 검사 끔 |
| `RAPIER_BODY_SORT`, `RAPIER_CONTACT_SORT` | 솔버 몸체 Morton 정렬 주기(기본 16), 색 안 접촉 정렬(기본 1). `0`이면 끔 |
| `RAPIER_SOLVER_DIAG` | 있으면 병렬 솔버 진단을 기록(`s_*` 열) |
| `RAPIER_STABLE_EPOCH` | `0`이면 몸체 갱신마다 활성 집합 세대를 올려 솔버 접촉 그래프를 매 단계 다시 만듦(Rapier 원래 동작, 기본 `1`) |
| `DX_RAPIER_RAYSIMD` | `0`이면 레이캐스트 후보 검사에 AVX2를 쓰지 않음 |
| `DX_RAPIER_RAYWALK` | `0`이면 레이캐스트를 예전처럼 선분 상자 전체로 찾음, `2`면 두 방식을 함께 돌려 결과 차이를 셈(`n_ray_*`) |
| `DX_RAPIER_SOFTCCD` | soft CCD 거리(기본 0, 끔) |
| `DX_RAPIER_CCD_SPLIT`, `DX_RAPIER_CCD_HARDCAT` | 좀비만 bullet CCD, 나머지 soft CCD(기본 끔) |
| `DX_RAPIER_SLEEP`, `DX_RAPIER_BATCH`, `DX_RAPIER_SOFTNESS` | `0`이면 잠자기·일괄 쓰기·접촉 강성 조정 끔 |
| `DX_RAPIER_AVX512`, `DX_RAPIER_AVX2` | `0`이면 그 빌드를 쓰지 않음 |
| `DX_RAPIER_PGS`, `DX_RAPIER_STAB` | 하위 단계 안의 PGS·안정화 반복 수 |
| `DX_RAPIER_SLOP`, `DX_RAPIER_PREDICT`, `DX_RAPIER_RECYCLE`, `DX_RAPIER_MAXCORR`, `DX_RAPIER_WARMSTART` | Rapier 접촉 파라미터 |
| `TABPERF_FASTTIMING` | `0`이면 게임 옵션 FastTimming을 따름(모드 기본은 옵션과 상관없이 빠른 타이머) |
| `TABPERF_LOGIC_LAG` | `engine.json`의 `logicMaxLag`와 같음. `1`이면 원작처럼 로직이 2틱 밀릴 때 시계를 세움(기본 3) |
| `TABPERF_SUCS` | `0`이면 물리 호출의 권한 검사 생략을 끔 |
| `TABPERF_FASTBODY` | `0`이면 Rapier 몸체 값 직접 접근을 끔 |
| `TABPERF_TICKPROF` | `1`이면 로직 틱 안의 시스템·컴포넌트 갱신을 메서드별로 재어 20초마다와 진단 기록 끝에 로그에 `tickprof` 줄로 남김. `full`이면 엔티티의 `OnUpdate` 재정의, `CMovable` 단계, 행동 트리 잎 노드도 잼(틱이 약 25% 길어짐) |
| `TABPERF_POSTPROF` | `1`이면 물리 대기 작업을 메서드별로 재어, 진단 기록이 끝날 때 로그에 `postprof:` 줄로 남김 |
| `TABPERF_SWARMFAST`, `TABPERF_DEVICECHECK`, `TABPERF_FASTREMOVE`, `TABPERF_FASTDRAW` | `0`이면 웨이브 미니맵 표시·D3D 장치 확인·루트 목록 지우기·단순 스프라이트 손질을 끔|
| `TABPERF_RENDERER`, `TABPERF_DXVK_FPS` | `engine.json`의 `renderer`·`dxvkMaxFps`와 같음. `dxvk`면 모드 폴더의 `dxvk\d3d9.dll`(DXVK)을 씀, 그때 진단 기록의 `engine` 열과 로그에 `+ DXVK`가 붙음 |
| `TABPERF_RENDERDUMP` | `1`이면 게임 렌더러(`DXRender_SlimDX`) 어셈블리의 형식·메서드 IL 크기와 쓰는 SlimDX 멤버를 `%APPDATA%\TABPerf\renderdump.txt`에 남김 |
| `TABPERF_SWARMCHECK`, `TABPERF_FASTDRAW_CHECK` | `1`이면 웨이브 표시·단순 스프라이트를 원작과 함께 계산해 차이를 로그에 남김(`swarm check:`, `fast draw check:`). 느려지므로 시간 비교에는 쓰지 말 것 |

각 설정의 배경과 측정 결과는 [DEVELOPMENT.md](DEVELOPMENT.md)를 보세요.
