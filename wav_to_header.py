import wave                          # standard library for reading .wav files
import struct                        # for unpacking raw binary sample data
import sys                           # for reading command line arguments
import os                            # for file path manipulation

def wav_to_header(input_wav, output_h=None):
    # if no output filename given, use the input name with .h extension
    if output_h is None:
        base     = os.path.splitext(input_wav)[0]   # strip .wav extension
        output_h = base + ".h"                       # replace with .h

    # open the wav file and read its properties
    with wave.open(input_wav, 'rb') as wf:
        sr_in   = wf.getframerate()      # sample rate in Hz
        sw      = wf.getsampwidth()      # bytes per sample (1=8bit, 2=16bit, 4=32bit)
        nframes = wf.getnframes()        # total number of sample frames
        raw     = wf.readframes(nframes) # read all raw bytes from the file

    # map sample width in bytes to struct format character for unpacking
    fmt = {1: 'b', 2: 'h', 4: 'i'}[sw]

    # unpack raw bytes into a list of integers
    samples = list(struct.unpack(f'<{len(raw)//sw}{fmt}', raw))

    num_samples = len(samples)           # total number of output samples

    # derive C variable name from filename — replace dashes and spaces with underscores
    var_name = os.path.splitext(os.path.basename(input_wav))[0].replace('-', '_').replace(' ', '_')

    # build the output file line by line
    lines = []
    lines.append('#include <stdint.h>')                                      # needed for int16_t type
    lines.append('')
    lines.append(f'/* {os.path.basename(input_wav)}')                       # comment header
    lines.append(f' * Sample rate : {sr_in} Hz')                            # sample rate info
    lines.append(f' * Bit depth   : {sw*8}-bit signed PCM (little-endian)') # bit depth info
    lines.append(f' * Channels    : 1 (mono)')                              # always mono
    lines.append(f' * Duration    : {num_samples/sr_in:.3f} seconds')       # duration in seconds
    lines.append(f' *')
    lines.append(f' * DAC write: DAC1->DHR12R1 = ((int32_t){var_name}[i] + 32768) >> 4;')
    lines.append(f' */')
    lines.append('')
    lines.append(f'#define FILE_SIZE  {num_samples}U')  # only define the file size
    lines.append('')
    lines.append(f'const int16_t {var_name}[] = {{')            # const array in flash

    cols = 8                                # samples per line
    for i in range(0, num_samples, cols):
        chunk = samples[i:i+cols]           # slice out one row
        row   = ','.join(f'{v:8d}' for v in chunk) + ','  # format each value
        lines.append(f'    {row}')          # indent with 4 spaces

    lines.append('};')                      # close the array
    lines.append('')                        # trailing newline

    # write to output file
    with open(output_h, 'w') as f:
        f.write('\n'.join(lines))

    # print summary
    print(f"Input:    {input_wav} ({sr_in} Hz, {nframes} frames, mono)")
    print(f"Output:   {output_h}")
    print(f"Samples:  {num_samples} @ {sr_in} Hz = {num_samples/sr_in:.3f}s")
    print(f"Size:     {num_samples*2} bytes ({num_samples*2/1024:.1f} KB)")

if __name__ == '__main__':
    if len(sys.argv) < 2:
        print("Usage: python wav_to_header.py input.wav [output.h]")
        sys.exit(1)

    input_wav = sys.argv[1]                                      # input wav path
    output_h  = sys.argv[2] if len(sys.argv) > 2 else None      # output .h path (optional)
    wav_to_header(input_wav, output_h)