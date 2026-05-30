import sigrokdecode as srd


class Decoder(srd.Decoder):
    api_version = 3
    id = 'infolithium'
    name = 'InfoLithium'
    longname = 'Sony InfoLithium custom 1-wire'
    desc = 'Reverse-engineered Sony InfoLithium 1-wire decoder.'
    license = 'gplv2+'
    inputs = ['logic']
    outputs = []
    tags = ['Embedded/industrial']
    channels = (
        {'id': 'bus',     'name': 'BUS', 'desc': 'shared 1-wire bus'},
        {'id': 'batside', 'name': 'BAT', 'desc': 'battery control line', 'optional': True},
    )

    # timing thresholds in microseconds
    INTERBYTE_US  = 5000
    INTERFRAME_US = 20000
    PREAMBLE_US   = 12000

    options = (
        {'id': 'baudrate', 'desc': 'Baud rate (bps)', 'default': 2041},
        {'id': 'battery_active_high', 'desc': 'Battery-side line active HIGH',
         'default': 'yes', 'values': ('yes', 'no')},
    )
    annotations = (
        ('bit',      'Bit'),
        ('byte',     'Byte'),
        ('role',     'Role'),
        ('source',   'Source'),
        ('frame',    'Frame'),
        ('preamble', 'Preamble'),
        ('warn',     'Warning'),
    )
    annotation_rows = (
        ('bits',    'Bits',   (0,)),
        ('bytes',   'Bytes',  (1,)),
        ('roles',   'Role',   (2,)),
        ('sources', 'Source', (3,)),
        ('frames',  'Frames', (4, 5, 6)),
    )

    def __init__(self):
        self.reset()

    def reset(self):
        self.samplerate      = None
        self.have_batside    = False
        self.bit_samp        = 0.0
        self.interbyte_samp  = 0
        self.interframe_samp = 0
        self.preamble_samp   = 0
        self.frame_idx       = 0
        self.byte_in_frame   = 0

    def metadata(self, key, value):
        if key == srd.SRD_CONF_SAMPLERATE:
            self.samplerate = value

    def start(self):
        self.out_ann = self.register(srd.OUTPUT_ANN)
        self.have_batside = self.has_channel(1)
        if not self.samplerate:
            raise srd.Error('need samplerate')

        self.bit_samp        = float(self.samplerate) / float(self.options['baudrate'])
        self.interbyte_samp  = int(self.samplerate * self.INTERBYTE_US  / 1_000_000)
        self.interframe_samp = int(self.samplerate * self.INTERFRAME_US / 1_000_000)
        self.preamble_samp   = int(self.samplerate * self.PREAMBLE_US   / 1_000_000)

    def put_ann(self, ss, es, ann, text):
        self.put(ss, es, self.out_ann, [ann, text])

    def _emit_frame_start(self, ss, high_gap):
        if high_gap >= self.interframe_samp or self.frame_idx == 0:
            self.frame_idx += 1
            self.byte_in_frame = 0
            self.put_ann(ss, ss, 4, [f'Frame {self.frame_idx}', f'F{self.frame_idx}'])

    def _role(self):
        return ('CMD1', 'CMD2', 'RESP1', 'RESP2')[self.byte_in_frame % 4]

    def _classify_source(self, bat_samples):
        if not self.have_batside:
            return None
        active_high = (self.options['battery_active_high'] == 'yes')
        active = sum(1 for s in bat_samples if s is not None and (bool(s) if active_high else not bool(s)))
        known  = sum(1 for s in bat_samples if s is not None)
        if known == 0:
            return 'unknown'
        if active == 0:
            return 'camera'
        if active >= 6:
            return 'battery'
        return 'mixed'

    def decode(self):
        pins = self.wait({'skip': 0})
        last_rise = self.samplenum if pins[0] else None

        while True:
            self.wait({0: 'f'})
            fall = self.samplenum
            high_gap = (fall - last_rise) if last_rise is not None else 0

            if high_gap < self.interbyte_samp:
                # data zero bit inside a byte, not a start bit
                self.wait({0: 'r'})
                last_rise = self.samplenum
                continue

            # start bit, sample 8 data bits
            bits = []
            bat_samples = []
            bit_spans = []
            for i in range(8):
                center = int(fall + (1.5 + i) * self.bit_samp)
                bit_ss = int(fall + (1.0 + i) * self.bit_samp)
                bit_es = int(fall + (2.0 + i) * self.bit_samp)
                p = self.wait({'skip': max(1, center - self.samplenum)})
                bits.append(p[0])
                bat_samples.append(p[1] if self.have_batside else None)
                bit_spans.append((bit_ss, bit_es, p[0]))

            # check if this is a preamble pulse (still low way past a byte)
            check = int(fall + self.preamble_samp * 0.75)
            p = self.wait({'skip': max(1, check - self.samplenum)})
            if p[0] == 0:
                self.wait({0: 'r'})
                last_rise = self.samplenum
                self.put_ann(fall, last_rise, 5, ['Preamble', 'PRE'])
                self.byte_in_frame = 0
                continue

            last_rise = int(fall + 9 * self.bit_samp)
            value = sum(b << i for i, b in enumerate(bits))
            byte_end = int(fall + 9 * self.bit_samp)

            self._emit_frame_start(fall, high_gap)
            role   = self._role()
            source = self._classify_source(bat_samples)

            for ss, es, b in bit_spans:
                self.put_ann(ss, es, 0, [str(b)])

            self.put_ann(fall, byte_end, 1, [f'0x{value:02X} ({value})', f'0x{value:02X}', f'{value:02X}'])
            self.put_ann(fall, byte_end, 2, [f'{role}: 0x{value:02X} ({value})', f'{role}'])
            if source:
                self.put_ann(fall, byte_end, 3, [source])

            self.byte_in_frame += 1
