# MOOD MKII Study — VST3 para Windows

Recriação **de estudo** do pedal *Chase Bliss MOOD MKII* (micro-looper: Drolo FX · canal wet: Old Blood Noise Endeavors) como plugin VST3.
Baseada no manual oficial, no vídeo de referência (MOOD em C++ no Daisy) e em testes de comportamento. **Não é o firmware original** (fechado):
é uma modelagem a partir do comportamento documentado. Onde o manual não diz, a decisão está marcada como *inferência* (seção 6).

## 1. Compilar (Windows 10/11)

Requisitos: **Visual Studio 2022** (carga "Desenvolvimento para desktop com C++"), **CMake ≥ 3.22**, **Git** e internet (o primeiro `configure` baixa o JUCE 8.0.4).

```bat
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release --target MoodMKIIStudy_VST3
```

* Resultado: `build\MoodMKIIStudy_artefacts\Release\VST3\MOOD MKII Study.vst3` (é uma pasta — copie inteira para `C:\Program Files\Common Files\VST3\`, ou para `%LOCALAPPDATA%\Programs\Common\VST3\` sem admin).
* Para testar sem DAW: `--target MoodMKIIStudy_Standalone`.
* Para depurar no VS: abra a pasta com *File ▸ Open ▸ CMake…* e escolha o alvo `MoodMKIIStudy_Standalone`.
* Licença do JUCE 8: AGPLv3 ou comercial (há camada gratuita). Para estudo/uso pessoal com código aberto, ok; confira os termos se for distribuir.

### 1b. Compilar na nuvem, sem instalar nada (GitHub Actions)

O projeto traz `.github/workflows/build-windows.yml` (cópia solta: `build-windows.yml`). Ele aceita o código de **3 jeitos** (nesta ordem): arquivos soltos com `CMakeLists.txt` na raiz · **só o `.zip` do projeto na raiz do repositório (o workflow extrai sozinho)** · pasta do projeto dentro de uma subpasta.

**Jeito mais curto (só o zip):**
1. Crie um repositório no GitHub (**público** = minutos grátis e ilimitados; privado = cota mensal do plano, e minutos de Windows contam em dobro).
2. *Add file ▸ Upload files* → arraste `MoodMKII_Study_VST3_project.zip` (sem extrair) → *Commit*.
3. *Add file ▸ Create new file* → digite o nome `.github/workflows/build-windows.yml` (as barras criam as pastas) → cole o conteúdo de `build-windows.yml` → *Commit*. Isso já dispara o build.
4. Aba **Actions ▸ Build Windows VST3** (ou *Run workflow*; se houver mais de um `.zip`, informe o nome no campo `zip_name`).
5. Ao terminar (✔ verde), abra a execução ▸ **Artifacts** ▸ `MOOD-MKII-Study-win64` (`MOOD MKII Study.vst3` + `.exe` Standalone). O artefato `MOOD-MKII-Study-demos` traz os renders `.wav` do núcleo.
6. Copie a pasta `MOOD MKII Study.vst3` para `C:\Program Files\Common Files\VST3\` e faça *rescan* no DAW.

Se ficar ❌, abra o passo vermelho e copie as linhas com `error`.
O workflow usa o runner `windows-2022` (Visual Studio 2022) de propósito: o `windows-latest` está migrando para uma imagem com Visual Studio 2026, um compilador novo demais para o JUCE 8.0.4 fixado aqui.

Teste do núcleo DSP (sem JUCE, roda em qualquer SO): `g++ -std=c++17 -O2 Tests/dsp_test.cpp -o dsp_test && ./dsp_test demos` (ou `-DMOOD_BUILD_DSP_TESTS=ON` no CMake).

## 2. Estrutura

```
Source/dsp/   núcleo DSP em C++17 puro (sem JUCE) — é onde está o "pedal"
  MoodCommon.h   utilitários (delay line, Hermite, softLimit…)
  MoodParams.h   EngineParams + mapeamentos (11 passos de clock, velocidades tape/slip/stretch…)
  MoodReverb.h   reverb plate de Dattorro com difusão variável (SMEAR)
  MoodWet.h      canal Wet: Reverb / Delay / Slip, freeze, trails, tone, spread
  MoodLooper.h   micro-looper "sempre ouvindo": Env / Tape / Stretch, overdub, fade, classic
  MoodEngine.h   clock (taxa de amostragem), roteamento Wet↔Loop, mix, ramping/expressão
Source/       PluginParameters.h · PluginProcessor.* · PluginEditor.*  (camada JUCE)
Tests/        dsp_test.cpp (período do loop vs clock, freeze estável, SPREAD, fuzz, desempenho)
demos/        renders offline do núcleo (pluck Karplus-Strong → motor)
```

## 3. Mapa pedal → plugin

**Knobs** (todos 0–1, automatáveis)

| Pedal | ID | Reverb / Env | Delay / Tape | Slip / Stretch |
|---|---|---|---|---|
| TIME (wet) | `wet_time` | tamanho (×0,30–1,35) **e** decay (0,28→1; ≥98,5 % = infinito) | tempo 48–65 000 amostras do clock (curva quadrática) | janela de amostragem 64–32 768 amostras (exp.) |
| MODIFY (wet) | `wet_modify` | SMEAR: 0 = multi-tap, 1 = reverb borrado | feedback (máx = 1,0 → repetições estáveis, empilham) | velocidade/direção em semitons ±12; meio = neutro |
| MIX | `mix` | crossfade *equal-power* entre entrada e MOOD (os dois canais) | | |
| CLOCK | `clock` | 11 passos 2k·3k·4k·6k·8k·12k·16k·24k·32k·48k·64k (ou contínuo com SMOOTH) | | |
| LENGTH (loop) | `loop_length` | Env: tamanho da fatia | Tape: encurta o loop | Stretch: tamanho da fatia |
| MODIFY (loop) | `loop_modify` | Env: sensibilidade | Tape: 8 passos em oitavas −4×…−0,5× / +0,5×…+4× | Stretch: meio = congelado, extremos = 1× (sem efeito), esquerda = reverso |

**Toggles**: `wet_mode` (Reverb/Delay/Slip) · `routing` (In / In+Loop / Loop — só vale com os dois canais ligados) · `loop_mode` (Env/Tape/Stretch) · `preset_sel` (Left/Live/Right).

**Footswitches** (na GUI; também são parâmetros automatáveis): esquerdo = `wet_on` (tap) e `wet_freeze` (segurar); direito = `loop_on` (tap: gravar ⇄ tocar) e `loop_overdub` (segurar). Com o dip **LATCH**, segurar passa a alternar.
LEDs: wet (vermelho; branco = freeze) · preset (âmbar) · loop (pisca no ritmo do comprimento do loop enquanto grava; verde = tocando; vermelho = overdub).

**Dip switches (config)**: CLASSIC · MISO · SPREAD · DRY KILL · TRAILS · LATCH · NO DUB · SMOOTH.
**Dip switches (ramping / EXP-CV)**: TIME · MODIFY(wet) · CLOCK · MODIFY(loop) · LENGTH · BOUNCE · SWEEP (T/B) · POLARITY (R/F) + `rmp_speed` (no pedal é o knob MIX) + `ctrl_source` (Ramp ⇄ EXP/CV) + `exp_cv`.

**Hidden options** (botão *HIDDEN OPTIONS* = "segurar os dois footswitches"): STEREO WIDTH (TIME) · TONE (MODIFY wet) · RAMP WAVEFORM (MIX: triângulo→quadrada→seno→random→random suave, com morph) · LEVEL BALANCE (CLOCK) · FADE (LENGTH) · DIRECT MICRO-LOOP (MODIFY loop) · SYNC (Unsynced / Loop→Wet / Wet→Loop) · SPREAD SOLO (Both / Wet / Loop) · LOOP LENGTH (Half / Full). *True Bypass* é um botão no topo. *Reset Hidden* restaura os padrões.

**Extras (não existem no pedal; só o painel EXTRAS da GUI)**: `in_level` (**INPUT**, −24…+12 dB, vale para dry e efeito) · `out_level` (**OUTPUT**) · `clock_degrade` (**DEGRADE**, padrão 10 %: 0 % = o clock só muda tempo/pitch/banda, sem "bitcrush"; 100 % = aliasing bruto do pedal) · `clock_noise` (**NOISE**, padrão 50 %: ruído suave que cresce quando o CLOCK desce) · `wet_lowcut` (**WET LOW CUT**, 20 Hz…790 Hz, padrão ≈70 Hz). Medidores IN/OUT e LED **LIMIT** (acende se o limitador interno atuar). Nenhum entra nos slots Left/Right.

### 3b. Presets

* **Slots do pedal (Left / Live / Right)**: toggle PRESET + botões *SAVE L / SAVE R* (guardam knobs, toggles, dips e hidden; Live restaura os knobs de antes). Ficam no estado do projeto da DAW.
* **Presets de usuário (arquivos)**: caixa no topo (digite um nome ▸ **SAVE**; **DEL** remove; **< >** navega; **DIR** abre a pasta). Ficam em `%APPDATA%\\MOOD MKII Study\\Presets\\*.moodpreset` (XML simples, dá para copiar entre PCs). Guardam tudo que é som, inclusive DEGRADE/NOISE/LOW CUT (não guardam INPUT/OUTPUT nem o estado dos footswitches).
* **Presets de fábrica**: 10 (aparecem também como *programs* na lista de presets do host).
* MIDI *Program Change* n carrega o n-ésimo preset da lista (fábrica primeiro, depois usuário).

### 3c. Todos os controles são mapeáveis

* **Todo controle da GUI é um parâmetro do plugin** (≈60): knobs, toggles, dips, hidden options, extras. Qualquer DAW consegue automatizar/mapear.
* **Gestos do footswitch como parâmetros momentâneos**: `fs_wet` e `fs_loop` (≥ 50 % = pressionado): toque curto = tap; segurar > 450 ms = hold (freeze / overdub; com LATCH alterna). A decodificação fica no plugin, então um botão de controlador mapeado nesses parâmetros se comporta exatamente como o pedal. Ações: `act_save_left`, `act_save_right`, `act_prev_preset`, `act_next_preset` (disparam na subida).
* **MIDI Learn embutido (funciona até no Standalone)**: **botão direito em qualquer controle** ▸ *MIDI Learn* ▸ mova um knob/botão do controlador. Vale para qualquer CC (0–127), um CC por parâmetro; o mapeamento é salvo no projeto/estado. Menu também: remover mapeamento, remover todos, restaurar padrão. No Standalone, habilite sua entrada MIDI em *Options* (engrenagem). O plugin declara entrada MIDI (VST3 efeito); em DAWs que não roteiam MIDI para efeitos, use o mapeamento da própria DAW sobre os parâmetros.

### 3d. Gravador / exportação de áudio

Na faixa inferior da GUI:
* **REC** começa a capturar a saída estéreo processada do plugin (inclui o loop, wet e dry conforme o MIX/roteamento).
* **STOP** encerra a captura. A gravação também para automaticamente ao atingir 180 segundos.
* **SALVAR WAV** exporta PCM 16-bit na taxa de amostragem do projeto/host.
* **SALVAR MP3** exporta em 128 kbit/s (qualidade média, arquivo menor). Para codificar MP3, é necessário instalar o encoder **LAME** e deixar o executável `lame` disponível no `PATH` do Windows. Se o LAME não estiver disponível, o plugin informa o erro; o WAV continua funcionando sem dependências extras.

O gravador usa armazenamento temporário PCM 16-bit (até 180 s) e não altera o áudio do plugin nem o estado do looper. Para salvar uma frase curta, inicie REC antes de tocar e use STOP assim que terminar.

## 4. Como o motor funciona

* **CLOCK = taxa de amostragem do motor.** O motor roda a `fc` (2–64 kHz) via acumulador de fase; quando `fc < fs` a entrada é decimada e a saída é *zero-order hold* — daí o aliasing/"computer noises". O **DEGRADE** põe um passa-baixas Butterworth de 4ª ordem na entrada (anti-alias) e na saída (reconstrução) com corte em `0,4·2^(5·d²)·clock` (d = DEGRADE): o padrão de 10 % deixa o aliasing ~20 dB abaixo do bruto (medido: a 2 kHz vai de +1 dB para −18 dB de energia inarmônica/harmônica), 100 % = sem filtro. O **NOISE** é gerado na taxa do clock (textura digital) e passa pelo filtro de reconstrução: ~−54 dBFS a 2 kHz, ~−65 dBFS a 16 kHz, zero a 64 kHz. Com `clock ≥ fs` (48k/64k) nenhum filtro atua. Tudo (delay, reverb, loop) é medido em *amostras do clock*, então mexer no CLOCK escala tempo e pitch de wet e loop juntos, e overdubs ficam no lugar. Verificado nos testes: período do loop = 65 536 / fc (1,024 s a 64k; 4,096 s a 16k).
* **Wet.** Reverb = Dattorro (tanque em 8, 4 difusores de entrada, 2 allpasses modulados); SMEAR escala os coeficientes dos allpasses (0 ⇒ só atrasos ⇒ cluster de ecos). Delay = 2 cabeças com crossfade (MKII, sem pitch-bend; trocar o TIME com feedback máx. "estica" o loop); Classic = glide do ponteiro (borrachudo). Slip = pitch-shifter de linha de atraso com 2 cabeças e janela sin², freeze = para de gravar e repete a janela. FREEZE: reverb com decay 1 e entrada muda; delay com feedback 1. A modulação do reverb tem taxa proporcional ao clock (desvio de pitch constante, ~2 cents, em todos os clocks) e o ganho de entrada acompanha o decay (nível de regime plano em todo o curso do TIME, medido com ruído **rosa** — com ruído branco a calibração anterior subestimava os graves). O tanque tem passa-altas (120 Hz; sai de cena no FREEZE) e o wet tem o *low cut* extra: reverb sem "lama" nos graves. SMEAR baixo = ecos nítidos e ~7 dB mais fortes que o reverb difuso. **Headroom**: o motor trabalha com −6 dB e a saída volta ao ganho original, então o limitador interno só atua acima de ≈ +5 dBFS (verificado com acordes de pico 0,8).
* **Looper.** Anel de 65 536 amostras sempre gravando (estado "ouvindo", `loop_on` = off). Ao ligar, o loop começa na amostra mais antiga e termina "agora". Desligar volta a gravar *por cima* do ponto de reprodução (função *replace*). Overdub grava só o sinal limpo (nunca o wet). Env = fatias que repetem enquanto há som na entrada; Tape = leitor com velocidade em oitavas e crossfade nas emendas; Stretch = granular (grãos Hann, hop = G/2) com cabeça de leitura de velocidade variável.
* **Roteamento.** Ordem por tick: looper → (roteamento) → wet → soma. O looper "ouvindo" grava `entrada + wet` (ambiência embutida; *trail catcher* funciona). Loop roteado pelo wet vira 100 % wet, a menos de DIRECT MICRO-LOOP.
* **Presets.** Left/Right guardam knobs, toggles, dips e hidden; voltar a Live restaura os knobs de antes. Salvar: botões *SAVE L / SAVE R* (no pedal: segurar 3 s + 3 s).

## 5. Classic Mode (aproximação)

Ruído que cresce com a queda do CLOCK · loop que se degrada a cada passada (filtro + ruído) · reverb sem modulação e com comb "ringy" · Slip sem interpolação (aliased) · delay com glide (pitch-bend) · Tape LENGTH vira *chop* · NO DUB muda a saída durante o overdub.

## 6. O que é inferência (confira com o pedal de verdade)

0. Os valores padrão de DEGRADE (10 %), NOISE (50 %) e WET LOW CUT são preferências de uso, não do pedal; DEGRADE 100 % reproduz o aliasing bruto.
1. Curvas e faixas de TIME/LENGTH/sensibilidade, tempo máximo de delay (65 000 amostras) e comprimento do loop (65 536 amostras FULL; 32 768 HALF — deduzido do vídeo e da opção LOOP LENGTH).
2. Posição do "1×" no knob Tape (aqui: 8 zonas iguais, o meio fica entre −0,5× e +0,5×; padrão do plugin 1× à frente) e assimetria do reverso no Slip.
3. Semântica exata de SWEEP/POLARITY (aqui: SWEEP define se o knob é topo ou fundo da faixa; POLARITY = subir/descer; *ramp* dispara quando o pedal é ligado) e de EXP/CV (mesmos dips; sem knob atribuído controla o MIX).
4. O que o looper grava com o wet ligado (aqui `entrada + wet`).
5. Algoritmos internos: o reverb real é da OBNE e o looper da Drolo; aqui são implementações próprias a partir do comportamento descrito.
6. **Não implementado**: o mapa de CCs *oficial* do pedal e o clock sync MIDI (use o MIDI Learn), entrada CV/expressão físicas (use `exp_cv`), qualidade "Classic" bit-a-bit, gestos de 3 s do pedal.
7. A camada JUCE (`Source/Plugin*`) **não foi compilada no ambiente onde foi escrita** (sem JUCE/Windows/rede); o núcleo DSP foi compilado e testado (ASan/UBSan). Se o compilador reclamar de algo no JUCE, corrija e siga — a lógica não depende disso.


## Gravador de saída

Os botões REC, STOP, SALVAR WAV e SALVAR MP3 ficam na faixa dedicada logo abaixo dos seletores REVERB/ROUTING/LOOP MODE, dentro da área principal do pedal. O gravador captura a saída estéreo processada do plugin por até 180 segundos. WAV é PCM 16-bit; MP3 usa 128 kbps e requer o executável LAME disponível no PATH do sistema.
