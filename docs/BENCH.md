# 성능 시험

모드에 들어 있는 게임 안 자동 성능 시험과, 이를 엔진별로 돌리고 정리하는 `scripts\bench.ps1` 사용법입니다. 측정 결과와 해석은 [DEVELOPMENT.md](DEVELOPMENT.md)에 있습니다.

## 시험 내용

자동 시험은 매번 같은 조건으로 게임을 진행합니다.

1. 시작 화면에서 점수 배율 900% 생존 게임을 시작합니다. 테마 「부식의 땅」(점수 배율이 가장 높은 테마), 감염체 최다(150%), 80일, 맵 시드 고정(`20261104`)이라 엔진마다 같은 맵입니다.
2. 시작 애니메이션이 끝나 입력이 켜지면 카메라를 게임의 최대 축소 폭(마우스 휠로 뺄 수 있는 끝)으로 맞추고 시험 내내 유지합니다(`-Zoom max`).
3. 레벨 시작 5초 뒤 피해를 끄고(`CLife.DamageAvailable`, 게임의 디버그 스위치) 마지막 웨이브를 4번 발생시킵니다. 생성기 4곳, 한 번에 생성기당 약 4,350마리입니다.
4. 좀비가 죽지 않고 지휘본부도 무너지지 않으므로, 좀비가 계속 늘며 지휘본부로 몰립니다.
5. 1초마다 CSV에 한 줄을 쓰고, 정한 시간이 지나면 게임을 끕니다.

## 준비

- `scripts\build.ps1`로 모드와 시험할 엔진을 빌드하고, `scripts\install.ps1`로 설치합니다. 시험할 엔진(`chipmunk`, `rapier`)이 `Mods\TABPerf\<엔진>\`에 있어야 합니다.
- Steam이 켜져 있어야 합니다.
- 게임이 꺼져 있어야 합니다.

게임은 보통 Steam을 거쳐 다시 시작되며, 이때 스크립트가 넘긴 환경 변수가 사라집니다. 그래서 스크립트는 실행하는 동안만 게임 폴더에 `steam_appid.txt`를 만들어 게임이 바로 실행되게 하고, 끝나면 지웁니다. 원래 있던 `steam_appid.txt`는 그대로 둡니다. 설정은 환경 변수로만 넘기므로 `engine.json`은 바꾸지 않습니다.

## 실행

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\bench.ps1
```

엔진마다 게임을 한 번씩 띄워 시험하고, 끝나면 결과를 표로 보여 줍니다. 한 번에 레벨 로딩을 포함해 약 3분이 걸립니다.

| 인자 | 기본값 | 뜻 |
| --- | --- | --- |
| `-Engine` | `chipmunk,rapier` | 시험할 엔진. `chipmunk`, `rapier`, `original`(게임 폴더의 원작 DLL)을 쉼표로 나열 |
| `-Runs` | `1` | 엔진마다 반복할 횟수 |
| `-Seconds` | `120` | 웨이브 발생 뒤 기록할 시간(초, 30 이상) |
| `-Factor` | `4` | 마지막 웨이브를 몇 번 발생시킬지(1~20) |
| `-Delay` | `5` | 레벨 시작부터 웨이브 발생까지(초) |
| `-Seed` | `20261104` | 생존 게임 맵 시드 |
| `-Threads` | `-1` (자동) | 물리 작업 스레드 수(`DX_SetOption(0)`), 레벨 시작 때 적용 |
| `-Zoom` | `max` | `max`는 가장 넓은 화면으로 고정, `off`는 카메라를 건드리지 않음 |
| `-Counters` | 끔 | Rapier 내부 구간 시간(`rs_*` 열)을 잼(`DX_RAPIER_COUNTERS=1`). 단계가 약 7% 길어짐 |
| `-OutDir` | `bench-out\<날짜-시각>` | 결과 폴더 |
| `-GameDir` | 자동으로 찾음 | 게임 설치 폴더 |

한 회가 `-Delay + -Seconds + 300`초 안에 끝나지 않으면 게임을 강제로 끄고 다음으로 넘어갑니다.

### 자주 쓰는 실행

```powershell
# 세 엔진 비교, 240초
.\scripts\bench.ps1 -Engine chipmunk,rapier,original -Seconds 240

# 렌더링 부담을 빼고 비교 (카메라 그대로)
.\scripts\bench.ps1 -Engine chipmunk,rapier -Zoom off

# 편차를 보려고 같은 조건 3번
.\scripts\bench.ps1 -Engine rapier -Runs 3

# 물리 작업 스레드 수 바꾸기
.\scripts\bench.ps1 -Engine rapier -Threads 8

# Rapier 내부 구간까지 (단계 시간 비교용으로는 쓰지 말 것)
.\scripts\bench.ps1 -Engine rapier -Counters
```

스크립트에 인자가 없는 설정은 환경 변수를 먼저 정하고 실행합니다. 게임은 스크립트의 환경을 물려받습니다.

```powershell
# Rapier 하위 단계를 10번으로 고정
$env:DX_RAPIER_ITERATIONS = '10'
.\scripts\bench.ps1 -Engine rapier -OutDir bench-out\fixed10
Remove-Item Env:DX_RAPIER_ITERATIONS
```

## 결과 파일

`-OutDir` 폴더에 남습니다.

- `<엔진>-<회차>.csv`: 1초마다 한 줄. 아래 [CSV 열](#csv-열) 참고.
- `summary.csv`: 실행마다 한 줄. 일시 정지가 아니고 물리 단계가 있었던 초만 씁니다.
  - `engine`, `run`, `dll`(쓰인 물리 DLL), `seconds`(쓴 초 수), `zombies_max`
  - `step_ms`, `frame_ms`: 전체 평균
  - `last30_*`: 마지막 30초 평균. 피해가 꺼져 좀비가 계속 늘므로 가장 무거운 구간입니다. `last30_zombies`, `last30_step_ms`, `last30_frame_ms`, `last30_steps_per_s`와, CSV의 `r_*`·`rs_*`·`g_*`·`n_*`·`l_*` 열 각각의 평균이 들어갑니다.

시험 중 로그는 `%APPDATA%\TABPerf\billions.log`에 남습니다(쓰인 물리 DLL, 카운터·하위 단계 설정, 결과가 없을 때의 원인).

## CSV 열

`r_*`, `rs_*`, `n_*`는 Rapier 판에서만 값이 있습니다. 시간은 따로 적지 않으면 그 1초 동안의 단계당 평균입니다.

### 기본

| 열 | 뜻 |
| --- | --- |
| `time` | 시각 |
| `engine` | 쓰인 물리 DLL |
| `paused` | 1이면 일시 정지 |
| `frames`, `frame_avg_ms`, `frame_max_ms` | 그 1초의 프레임 수, 평균·최대 프레임 시간 |
| `phys_steps`, `step_avg_ms`, `step_max_ms` | 물리 단계 수, `Space_Step` 한 번의 평균·최대 시간 |
| `zombies` | 좀비 수 |
| `note` | 표시 줄(`swarm x4` 웨이브 발생, `end` 끝). 정리할 때는 빼고 씀 |

### 래퍼 구간 (`r_*`)

| 열 | 뜻 |
| --- | --- |
| `r_push_ms` | 넣기(게임 값을 Rapier로) |
| `r_step_ms` | Rapier 단계(`r2Step`) |
| `r_pull_ms` | 가져오기(Rapier 값을 게임으로) |
| `r_pairs_ms` | 충돌 시작·분리 콜백 처리 |
| `r_index_ms` | 질의 인덱스 만들기 |
| `r_substeps` | 단계당 하위 단계 수(자동 조절 확인용) |
| `r_contact_pairs` | 단계당 접촉 쌍 수 |

### Rapier 내부 구간 (`rs_*`, `-Counters`일 때)

`rs_total_ms`(단계 전체), `rs_user_ms`(사용자 변경), `rs_update_ms`, `rs_bp_ms`·`rs_bp_final_ms`(넓은 범위 충돌 검사), `rs_np_ms`(좁은 범위 충돌 검사), `rs_island_ms`(섬 구성), `rs_collect_ms`(섬 제약 수집), `rs_solver_ms`(솔버 전체), `rs_vassembly_ms`(속도 조립), `rs_vsolve_ms`(속도 풀이), `rs_vupdate_ms`, `rs_writeback_ms`, `rs_ccd_ms`(연속 충돌 검사), `rs_pairs`(접촉 쌍), `rs_contacts`(접촉 수).

### 게임 쪽 시간 (`g_*`)

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

### 몸체 생성 (`n_*`)

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

### 공간 질의 (`q_*`)

종류(`area` 영역, `radius` 반경, `fast` 반경 배열, `near` 가장 가까운 몸체, `ray` 레이캐스트, `space` 남은 공간)마다 그 1초 기준입니다(Rapier 판만). 이 열과 `n_angle_*`~`n_sensor_*`는 질의·호출마다 시계를 읽어서 평소에는 꺼져 있고, 벤치를 돌릴 때만 켜집니다(`DX_SetOption(356, 1)`).

| 열 | 뜻 |
| --- | --- |
| `q_<종류>_per_s`, `q_<종류>_ms` | 호출 수, 합계 시간(ms, 모든 스레드 합) |
| `q_<종류>_lock_ms`, `q_<종류>_cb_ms` | 그중 인덱스 락 대기, 게임 콜백 시간(ms) |
| `q_<종류>_cand`, `q_<종류>_hits` | 한 번에 살펴본 인덱스 항목 수, 상자·마스크를 통과한 수 |

### 로직 갱신 (`l_*`)

렌더 스레드는 프레임마다 `DXGame.UpdateGameLogic`을 불러, 이전 로직 작업을 기다린 뒤 밀린 33.3 ms 틱을 처리할 작업을 띄웁니다. 모두 그 1초 기준입니다.

| 열 | 뜻 |
| --- | --- |
| `l_calls`, `l_wait_ms` | `UpdateGameLogic` 호출 수, 이전 작업을 기다린 평균 시간 |
| `l_tasks`, `l_start_ms`, `l_task_ms` | 로직 작업 수, 시작 지연 평균, 실행 시간 평균 |
| `l_ticks`, `l_idle` | 처리한 로직 틱 수, 틱 없이 끝난 작업 수 |
| `l_block_ia_ms`, `l_block_phys_ms`, `l_run_ms` | 게임 시계 루프가 로직 지연·물리 지연으로 멈춘 시간과 흐른 시간(ms). 모드의 빠른 타이머 루프에서만 값이 있음 |

## 실험용 환경 변수

`bench.ps1`에 인자가 없는 설정입니다. 실행 전에 정하면 게임에 넘어갑니다. 켜기/끄기 값은 모드 설정(`TABPERF_*`)은 `1`/`0`, `on`/`off`, `true`/`false`, `yes`/`no`를, 물리 DLL 설정(`DX_*`)은 `1`/`0`을 받습니다.

| 변수 | 뜻 |
| --- | --- |
| `DX_PHYS_THREADS` | 물리 작업 스레드 수(`0`은 작업 스레드 끔). `-Threads`가 있으면 그쪽이 우선 |
| `DX_RAPIER_ITERATIONS` | Rapier 하위 단계 수 고정(1~30). 자동 조절도 꺼짐 |
| `DX_RAPIER_ADAPT` | `0`이면 하위 단계 자동 조절 끔(게임 값 고정) |
| `RAPIER_SYNC_SPIN` | Rapier 솔버 작업 스레드가 단계를 기다릴 때 잠들기 전 spin 횟수(기본 2000, `0`이면 잠들지 않음) |
| `DX_RAPIER_ADAPT_MIN`, `DX_RAPIER_ADAPT_MAX` | 자동 조절 범위(기본 5~20) |
| `DX_RAPIER_COUNTERS` | `1`이면 내부 구간 시간(`-Counters`와 같음) |
| `DX_RAPIER_CCD` | `0`이면 연속 충돌 검사 끔 |
| `DX_RAPIER_RAYSIMD` | `0`이면 레이캐스트 후보 검사에 AVX2를 쓰지 않음 |
| `DX_RAPIER_RAYWALK` | `0`이면 레이캐스트를 예전처럼 선분 상자 전체로 찾음, `2`면 두 방식을 함께 돌려 결과 차이를 셈(`n_ray_*`) |
| `DX_RAPIER_SOFTCCD` | soft CCD 거리(기본 0, 끔) |
| `DX_RAPIER_CCD_SPLIT`, `DX_RAPIER_CCD_HARDCAT` | 좀비만 bullet CCD, 나머지 soft CCD(기본 끔) |
| `DX_RAPIER_SLEEP`, `DX_RAPIER_BATCH`, `DX_RAPIER_SOFTNESS` | `0`이면 잠자기·일괄 쓰기·접촉 강성 조정 끔 |
| `DX_RAPIER_AVX512`, `DX_RAPIER_AVX2` | `0`이면 그 빌드를 쓰지 않음 |
| `DX_RAPIER_PGS`, `DX_RAPIER_STAB` | 하위 단계 안의 PGS·안정화 반복 수 |
| `DX_RAPIER_SLOP`, `DX_RAPIER_PREDICT`, `DX_RAPIER_RECYCLE`, `DX_RAPIER_MAXCORR`, `DX_RAPIER_WARMSTART` | Rapier 접촉 파라미터 |
| `TABPERF_FASTTIMING` | `0`이면 게임 옵션 FastTimming을 따름(모드 기본은 옵션과 상관없이 빠른 타이머) |
| `TABPERF_SUCS` | `0`이면 물리 호출의 권한 검사 생략을 끔 |
| `TABPERF_FASTBODY` | `0`이면 Rapier 몸체 값 직접 접근을 끔 |
| `TABPERF_POSTPROF` | `1`이면 물리 대기 작업을 메서드별로 재어, 시험이 끝날 때 로그에 `postprof:` 줄로 남김 |

각 설정의 배경과 측정 결과는 [DEVELOPMENT.md](DEVELOPMENT.md)를 보세요.

## 결과 읽는 법

- **좀비 수로 맞춰 비교합니다.** 좀비가 생기는 속도는 게임이 얼마나 빨리 진행되느냐에 따라 다릅니다. 게임 속도가 다른 설정끼리는(예: 빠른 타이머 켬과 끔) 같은 120초에 좀비 수가 크게 달라집니다(7만 대 9만). 이때 `last30_*`는 다른 부하를 비교하게 되므로, 실행별 CSV를 좀비 수 구간(예: 1만 마리 단위)으로 묶어 평균을 냅니다.
- **게임 속도는 `g_clock_rate`와 `phys_steps`로 봅니다.** 정상 속도면 초당 물리 단계가 12, 로직 갱신(`g_ia_per_s`)이 30입니다. 단계 시간이 짧아져도 게임이 다른 이유로 느리면 `phys_steps`가 그만큼 늘지 않습니다.
- **한 번 실행은 약 2.5% 흔들립니다.** 같은 조건 두 번 사이의 차이입니다. 이보다 작은 차이는 `-Runs`로 여러 번 돌려 확인합니다.
- **카운터는 단계를 약 7% 늘립니다.** `-Counters` 실행끼리만 비교하고, 카운터를 끈 실행과 단계 시간을 섞어 비교하지 않습니다.
- **`-Zoom max`는 렌더링 부담이 큽니다.** 화면에 좀비를 모두 그리므로 프레임 시간이 늘어납니다(7만 마리에서 31 ms 대 56~70 ms). 물리만 비교하려면 `-Zoom off`가 낫습니다.

## engine.json으로 직접 실행

스크립트 없이 게임을 직접 실행할 때는 `%APPDATA%\TABPerf\engine.json`의 성능 시험 설정을 씁니다. `bench`만 켜면 게임 중 Ctrl+Shift+F9로 마지막 웨이브를 바로 발생시키고 1초마다 기록합니다. `benchAuto`까지 켜면 위 [시험 내용](#시험-내용)대로 자동으로 진행하고 게임을 끕니다. 기록은 기본으로 `%APPDATA%\TABPerf\bench.csv`에 남습니다.

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

| 키 | 환경 변수 | 기본값 | 의미 |
| --- | --- | --- | --- |
| `physicsEngine` | `TABPERF_ENGINE` | `chipmunk` | 물리 엔진 |
| `bench` | `TABPERF_BENCH` | `false` | 성능 시험 모드. Ctrl+Shift+F9 트리거와 초당 기록 |
| `benchAuto` | `TABPERF_BENCH_AUTO` | `false` | 자동 시험. 생존 게임 자동 시작, 피해 끔, 웨이브 발생, 시간이 되면 게임 종료 |
| `benchSwarmFactor` | `TABPERF_BENCH_FACTOR` | `4` | 마지막 웨이브를 몇 번 발생시킬지(1~20) |
| `benchSeconds` | `TABPERF_BENCH_SECONDS` | `240` | 웨이브 발생 뒤 기록할 시간(초, 30 이상) |
| `benchSwarmDelay` | `TABPERF_BENCH_DELAY` | `5` | 레벨 시작부터 웨이브 발생까지(초) |
| `benchSeed` | `TABPERF_BENCH_SEED` | `20261104` | 생존 게임 맵 시드 |
| `benchThreads` | `TABPERF_BENCH_THREADS` | 자동 | 물리 작업 스레드 수(`DX_SetOption(0)`). 레벨 시작 때 적용 |
| `benchZoom` | `TABPERF_BENCH_ZOOM` | `"max"` | `max`는 가장 넓은 화면으로 고정, `off`는 손대지 않음 |
| `benchCsv` | `TABPERF_BENCH_CSV` | `%APPDATA%\TABPerf\bench.csv` | 초당 기록 파일 경로 |

환경 변수가 `engine.json`보다 우선합니다. 스크립트의 기본 시험 시간(120초)은 `engine.json`의 기본값(240초)과 다릅니다.
