# fish completion for lyricsync

complete -c lyricsync -s h -l help -d 'show this help'
complete -c lyricsync -l denoise -d 'run denoising inference mode'
complete -c lyricsync -l keep-temp-files -d 'keep generated temporary files'
complete -c lyricsync -l romanize -d 'select ICU romanization'

complete -c lyricsync -l input-song -r -F -d 'original song to process'
complete -c lyricsync -l input-vocals -r -F -d 'already extracted vocals to use'
complete -c lyricsync -l output-vocals -r -F -d 'save extracted vocals at path'
complete -c lyricsync -l input-lyrics -r -F -d 'plain-text lyrics to align'
complete -c lyricsync -l output-lrc -r -F -d 'synced lyrics output path'
complete -c lyricsync -l model-vocal -r -F -d 'MDX-Net ONNX model'
complete -c lyricsync -l model-ctc -r -F -d 'CTC ONNX model'
complete -c lyricsync -l tokenizer -r -F -d 'CTC tokenizer tokens file'
complete -c lyricsync -l ffmpeg -r -F -d 'ffmpeg executable'
complete -c lyricsync -l temp-dir -r -F -d 'temporary directory'
complete -c lyricsync -l ctc-debug-dump -r -F -d 'write CTC parity debug dump'
complete -c lyricsync -l vocals-output -r -F -d 'save extracted vocals at path'
complete -c lyricsync -s l -l lyrics -r -F -d 'plain-text lyrics to align'
complete -c lyricsync -s o -l output -r -F -d 'synced lyrics output path'

complete -c lyricsync -l onnx-provider -x -f -a 'auto cpu cuda' \
    -d 'ONNX provider'
complete -c lyricsync -l vocals-format -x -f -a 'wav flac mp3 opus' \
    -d 'extracted vocals container'
complete -c lyricsync -l format -x -f -a 'wav flac mp3 opus' \
    -d 'extracted vocals container'
complete -c lyricsync -l model-output -x -f -a 'vocals instrumental' \
    -d 'model output stem'
complete -c lyricsync -l clip-mode -x -f -a 'clamp none' \
    -d 'final clipping policy'
complete -c lyricsync -l split-size -x -f -a 'current word char sentence' \
    -d 'lyrics split size'
complete -c lyricsync -l star-frequency -x -f -a 'none edges segment' \
    -d 'star-token placement'
complete -c lyricsync -l romanization -x -f -a 'off icu' \
    -d 'romanization backend'
complete -c lyricsync -l emissions -x -f \
    -a 'log-probabilities logits probabilities' \
    -d 'model emission values'

complete -c lyricsync -l onnx-device -x -f -d 'CUDA device id'
complete -c lyricsync -l chunk-seconds -x -f -d 'MDX chunk size in seconds'
complete -c lyricsync -l margin-seconds -x -f -d 'MDX chunk margin in seconds'
complete -c lyricsync -l compensate -x -f -d 'output gain'
complete -c lyricsync -l n-fft -x -f -d 'STFT size'
complete -c lyricsync -l hop -x -f -d 'STFT hop'
complete -c lyricsync -l dim-f -x -f -d 'override model frequency bins'
complete -c lyricsync -l dim-t -x -f -d 'override model time frames'
complete -c lyricsync -l language -x -f -d '3-letter language code'
