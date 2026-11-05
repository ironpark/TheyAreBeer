They Are Billions 최적화 패치(TABPerf). 이 저장소의 소스로 GitHub Actions가 빌드한 모드 압축 파일입니다.

**설치**
1. 게임에 [TABModLoader](https://codeberg.org/JKstring/TABModLoader)v1.1.6을 설치합니다(v1.1.7은 아직 지원하지 않음).
2. 압축 파일 안의 `TABPerf` 폴더를 `<게임 설치 폴더>\Mods\`에 그대로 넣습니다(`Mods\TABPerf\TABPerf.dll`이 되도록).
3. 게임을 실행합니다. 적용 기록은 `%APPDATA%\TABPerf\billions.log`에 남습니다.

**들어 있는 것**
- 엔진 모드 `TABPerf.dll`
- 게임 옵션 창의 **최적화** 탭(물리 엔진, 렌더러, DXVK 최대 FPS 등)
- 물리 엔진: Chipmunk2D 다중 코어(기본), Rapier 2D(`engine.json`의 `"physicsEngine": "rapier"`)
- 그래픽: DXVK 3.1.1(기본, `"renderer": "d3d9"`로 끔)
- 라이선스 문서(`licenses\`)

설정, 성능 측정, 변경 기록은 [README](https://github.com/ironpark/TheyAreBeer#readme)와 [CHANGELOG](https://github.com/ironpark/TheyAreBeer/blob/main/CHANGELOG.md)에 있습니다.
