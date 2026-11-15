# GitHub Actions — CI build (ako pridať)

Tento súbor **nie je aktívny** — GitHub blokuje push workflow súborov bez
`workflow` scope v tokenе. Ak chceš CI build, urob toto:

## Možnosť A — pridať scope a aktivovať

```bash
gh auth refresh -h github.com -s workflow
```

Potom:

```bash
mkdir -p .github/workflows
git mv examples-ci/github-actions-build.yml .github/workflows/build.yml
git commit -m "Aktivovany CI build"
git push
```

## Možnosť B — pridať súbor priamo na GitHube

1. Otvor https://github.com/trucknavigacie-cmyk/esp32s3-cam-stream/new/main/.github/workflows
2. Názov: `build.yml`
3. Skopíruj obsah `examples-ci/github-actions-build.yml`
4. Commit

## Čo CI robí

Pri každom pushi do `main` (ak sa zmenil `ESP32S3_CAM_WEB/**`):

1. Nainštaluje `arduino-cli` (latest)
2. Pridá `https://espressif.github.io/arduino-esp32/package_esp32_index.json`
3. Nainštaluje `esp32:esp32@3.3.12`
4. Skompiluje s plným FQBN
5. Uloží binárky ako artifact `firmware-<sha>` (30 dní):
   - `ESP32S3_CAM_WEB.ino.bin`
   - `ESP32S3_CAM_WEB.ino.merged.bin`
   - `ESP32S3_CAM_WEB.ino.bootloader.bin`
   - `ESP32S3_CAM_WEB.ino.partitions.bin`

Manuálne spustenie: Actions → **Build firmware** → Run workflow.

## Súbor

[github-actions-build.yml](github-actions-build.yml)
