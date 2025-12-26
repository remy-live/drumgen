#include <lv2/core/lv2.h>
#include <lv2/atom/atom.h>
#include <lv2/atom/util.h>
#include <lv2/atom/forge.h>
#include <lv2/midi/midi.h>
#include <lv2/urid/urid.h>
#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdbool.h>

#define DRUMGEN_URI "http://moddevices.com/plugins/mod-devel/drumgen"

enum {
    ROCK_CL=0, HARD_ROCK, PUNK, FUNK, DISCO,
    HOUSE, DEEP_HOUSE, TECHNO, DNB, JUNGLE,
    HIPHOP, TRAP, REGGAE, SKA, BOSSA,
    SWING_JAZZ, SHUFFLE, METAL, GABBER, AFROBEAT,
    TEST_KICK, TEST_SNARE, TEST_HATS, TEST_TOMS
};

#define M_KICK      (1 << 0)
#define M_SNARE     (1 << 1)
#define M_HAT       (1 << 2)
#define M_OPEN      (1 << 3)
#define M_TOM_HI    (1 << 4)
#define M_TOM_LO    (1 << 5)
#define M_CRASH     (1 << 6)
#define M_RIDE      (1 << 7)
#define M_CLAP      (1 << 8)
#define M_RIM       (1 << 9)
#define M_HAT_PEDAL (1 << 10)
#define M_RIDE_BELL (1 << 11)

typedef struct {
    LV2_Atom_Sequence* midi_out;
    float* bpm_port;
    float* style_port;
    float* density_port;
    float* measures_port;
    float* division_port;

    float* note_kick; float* note_snare; float* note_hat; float* note_open;
    float* note_clap; float* note_tom_hi; float* note_tom_lo; float* note_crash;
    float* note_ride; float* note_rim;

    float* progress_port; 
    
    // PORTS LIVE
    float* humanize_port;
    float* swing_port;
    float* mute_kick;
    float* mute_snare;
    float* mute_hats;
    float* mute_percs;

    LV2_URID_Map* map;
    LV2_Atom_Forge forge;
    LV2_Atom_Forge_Frame frame;
    LV2_URID midi_MidiEvent;

    double rate;
    double sample_counter;
    int current_step;
    int total_steps;
    
    uint16_t pattern[128]; 

    int current_playing_style;
    int requested_style;
    int last_density;
    int last_measures;
    bool change_pending;
} Drumgen;

// --- HELPERS ---
static bool is_rock_family(int s) { return (s==ROCK_CL || s==HARD_ROCK || s==PUNK || s==METAL || s==FUNK || s==SKA); }
static bool is_house_family(int s) { return (s==HOUSE || s==DEEP_HOUSE || s==DISCO || s==TECHNO); }
static bool is_urban_family(int s) { return (s==HIPHOP || s==TRAP || s==DNB || s==JUNGLE); }

static void fill_inst(Drumgen* self, uint16_t inst_mask, int interval, int offset, int max_steps) {
    for (int s = offset; s < max_steps; s += interval) {
        self->pattern[s] |= inst_mask;
    }
}

static void build_pattern(Drumgen* self, int style) {
    int d = (int)*(self->density_port);
    int measures = (int)*(self->measures_port);
    
    // Correction indentation
    if (measures < 1) measures = 1; 
    if (measures > 8) measures = 8;
    
    self->total_steps = measures * 16;
    for (int i = 0; i < 128; i++) self->pattern[i] = 0;

    switch(style) {
        case ROCK_CL: for(int m=0;m<measures;m++){int o=m*16; self->pattern[o+0]|=M_KICK; self->pattern[o+10]|=M_KICK; self->pattern[o+4]|=M_SNARE; self->pattern[o+12]|=M_SNARE;} if(d>=2) fill_inst(self,M_HAT,2,0,self->total_steps); break;
        case HARD_ROCK: for(int m=0;m<measures;m++){int o=m*16; self->pattern[o+0]|=M_KICK; self->pattern[o+8]|=M_KICK; if(d>4){self->pattern[o+10]|=M_KICK;self->pattern[o+12]|=M_KICK;} self->pattern[o+4]|=M_SNARE;self->pattern[o+12]|=M_SNARE;} fill_inst(self,M_HAT,2,0,self->total_steps); break;
        case PUNK: fill_inst(self,M_KICK,2,0,self->total_steps); fill_inst(self,M_SNARE,4,2,self->total_steps); fill_inst(self,M_HAT,2,0,self->total_steps); if(d>6)fill_inst(self,M_HAT,1,0,self->total_steps); break;
        case METAL: fill_inst(self,M_KICK,2,0,self->total_steps); if(d>7)fill_inst(self,M_KICK,1,0,self->total_steps); fill_inst(self,M_SNARE,8,4,self->total_steps); fill_inst(self,M_HAT,2,0,self->total_steps); break;
        case HOUSE: fill_inst(self,M_KICK,4,0,self->total_steps); if(style==HOUSE){if(d>=3)fill_inst(self,M_HAT,4,2,self->total_steps);if(d>=5)fill_inst(self,M_CLAP,8,4,self->total_steps);if(d>=7)fill_inst(self,M_HAT,2,0,self->total_steps);} break;
        case DISCO: fill_inst(self,M_KICK,4,0,self->total_steps); fill_inst(self,M_SNARE,8,4,self->total_steps); fill_inst(self,M_HAT,2,0,self->total_steps); if(d>3)fill_inst(self,M_OPEN,4,2,self->total_steps); break;
        case TECHNO: fill_inst(self,M_KICK,4,0,self->total_steps); if(d>=4)fill_inst(self,M_TOM_LO,4,3,self->total_steps); if(d>=6)fill_inst(self,M_HAT,2,0,self->total_steps); break;
        case DEEP_HOUSE: for(int m=0;m<measures;m++){int o=m*16; self->pattern[o+0]|=M_KICK; self->pattern[o+8]|=M_KICK; if(d>4){self->pattern[o+4]|=M_KICK;self->pattern[o+12]|=M_KICK;} if(d>3){self->pattern[o+4]|=M_RIM;self->pattern[o+12]|=M_RIM;} if(d>6){self->pattern[o+2]|=M_HAT;self->pattern[o+6]|=M_HAT;}} break;
        case DNB: for(int m=0;m<measures;m++){int o=m*16; self->pattern[o+0]|=M_KICK; self->pattern[o+10]|=M_KICK; self->pattern[o+4]|=M_SNARE; self->pattern[o+12]|=M_SNARE;} if(d>=4)fill_inst(self,M_HAT,2,0,self->total_steps); break;
        case JUNGLE: for(int m=0;m<measures;m++){int o=m*16; self->pattern[o+0]|=M_KICK; self->pattern[o+10]|=M_KICK; if(d>60)self->pattern[o+3]|=M_KICK; self->pattern[o+4]|=M_SNARE; self->pattern[o+14]|=M_SNARE; if(d>40){self->pattern[o+7]|=M_SNARE;self->pattern[o+9]|=M_SNARE;}} break;
        case HIPHOP: for(int m=0;m<measures;m++){int o=m*16; self->pattern[o+0]|=M_KICK; if(style==HIPHOP){if(d>3)self->pattern[o+10]|=M_KICK;if(d>7)self->pattern[o+7]|=M_KICK;self->pattern[o+4]|=M_SNARE;self->pattern[o+12]|=M_SNARE;}} if(style==HIPHOP)fill_inst(self,M_HAT,2,0,self->total_steps); break;
        case TRAP: for(int m=0;m<measures;m++){int o=m*16; self->pattern[o+0]|=M_KICK; self->pattern[o+8]|=M_SNARE;} if(d>=3)fill_inst(self,M_HAT,2,0,self->total_steps); if(d>=6)fill_inst(self,M_HAT,1,0,self->total_steps); break;
        case FUNK: for(int m=0;m<measures;m++){int o=m*16; self->pattern[o+0]|=M_KICK; if(d>4)self->pattern[o+10]|=M_KICK; if(d>7)self->pattern[o+14]|=M_KICK; self->pattern[o+4]|=M_SNARE;self->pattern[o+12]|=M_SNARE; if(d>6){self->pattern[o+7]|=M_RIM;self->pattern[o+9]|=M_RIM;}} fill_inst(self,M_HAT,2,0,self->total_steps); break;
        case REGGAE: for(int m=0;m<measures;m++){ self->pattern[m*16+8]|=M_KICK; self->pattern[m*16+8]|=M_RIM; } if(d>=4)fill_inst(self,M_HAT,2,0,self->total_steps); break;
        case SKA: fill_inst(self,M_KICK,4,0,self->total_steps); fill_inst(self,M_RIM,8,4,self->total_steps); if(d>3)fill_inst(self,M_HAT,4,2,self->total_steps); break;
        case BOSSA: for(int m=0;m<measures;m++){int o=m*16; self->pattern[o+0]|=M_KICK; self->pattern[o+10]|=M_KICK; self->pattern[o+4]|=M_RIM; self->pattern[o+7]|=M_RIM; self->pattern[o+12]|=M_RIM;} if(d>4)fill_inst(self,M_RIDE,2,0,self->total_steps); break;
        case SWING_JAZZ: for(int m=0;m<measures;m++){int o=m*16; self->pattern[o+0]|=M_KICK; self->pattern[o+8]|=M_KICK; if(d>5){self->pattern[o+4]|=M_KICK;self->pattern[o+12]|=M_KICK;}} if(d>2)fill_inst(self,M_RIDE,2,0,self->total_steps); if(d>6)fill_inst(self,M_HAT_PEDAL,8,4,self->total_steps); break;
        case SHUFFLE: for(int m=0;m<measures;m++){int o=m*16; self->pattern[o+0]|=M_KICK; self->pattern[o+8]|=M_KICK; self->pattern[o+4]|=M_SNARE; self->pattern[o+12]|=M_SNARE; if(d>3){self->pattern[o+0]|=M_HAT;self->pattern[o+3]|=M_HAT;self->pattern[o+4]|=M_HAT;self->pattern[o+7]|=M_HAT;}} break;
        case GABBER: fill_inst(self,M_KICK,4,0,self->total_steps); if(d>5)fill_inst(self,M_CRASH,4,0,self->total_steps); if(d>8)fill_inst(self,M_KICK,4,2,self->total_steps); break;
        case AFROBEAT: for(int m=0;m<measures;m++){int o=m*16; self->pattern[o+0]|=M_KICK; self->pattern[o+6]|=M_KICK; self->pattern[o+8]|=M_KICK; self->pattern[o+4]|=M_SNARE;} if(d>4)fill_inst(self,M_HAT,2,0,self->total_steps); if(d>7)fill_inst(self,M_RIDE_BELL,4,0,self->total_steps); break;
        case TEST_KICK: fill_inst(self, M_KICK, 4, 0, self->total_steps); return; 
        case TEST_SNARE: fill_inst(self, M_SNARE, 4, 0, self->total_steps); return; 
        case TEST_HATS: fill_inst(self, M_HAT, 2, 0, self->total_steps); return; 
        case TEST_TOMS: for(int s=0; s<self->total_steps; s+=4) { if((s/4)%2==0) self->pattern[s]|=M_TOM_HI; else self->pattern[s]|=M_TOM_LO; } return; 
    }

    for(int m=0; m<measures; m++) {
        int o = m * 16;
        bool isLast = (m == measures - 1);
        bool isEven = (m % 2 != 0);      
        bool isCycle = ((m + 1) % 4 == 0); 

        if(isEven && d >= 6) {
            if(is_rock_family(style) || style == DNB) { 
                self->pattern[o+7] |= M_SNARE; self->pattern[o+3] |= M_KICK;
            }
            if(is_house_family(style)) { 
                self->pattern[o+1] |= M_HAT; self->pattern[o+3] |= M_HAT;
            }
        }
        if((isLast || (isCycle && d >= 5)) && d >= 5) {
            int fs = o + (d >= 8 ? 8 : 12);
            for(int s=fs; s < o+16; s++) { self->pattern[s] &= ~(M_HAT|M_KICK|M_SNARE); }
            if(is_rock_family(style)) { self->pattern[fs] |= M_SNARE; self->pattern[fs+2] |= M_TOM_HI; }
            else if(style == TRAP || is_urban_family(style)) { self->pattern[fs] |= M_SNARE; self->pattern[fs+1] |= M_SNARE; self->pattern[fs+2] |= M_SNARE; }
            else { self->pattern[fs] |= M_SNARE; self->pattern[fs+2] |= M_SNARE; self->pattern[fs+2] |= M_CLAP; }
        }
    }
}

static LV2_Handle instantiate(const LV2_Descriptor* descriptor, double rate, const char* path, const LV2_Feature* const* features) {
    Drumgen* self = (Drumgen*)calloc(1, sizeof(Drumgen));
    self->rate = rate;
    for (int i = 0; features[i]; ++i) { if (!strcmp(features[i]->URI, LV2_URID__map)) { self->map = (LV2_URID_Map*)features[i]->data; } }
    if (!self->map) return NULL;
    lv2_atom_forge_init(&self->forge, self->map);
    self->midi_MidiEvent = self->map->map(self->map->handle, LV2_MIDI__MidiEvent);
    self->current_step = 0;
    self->current_playing_style = -1;
    return (LV2_Handle)self;
}

static void connect_port(LV2_Handle instance, uint32_t port, void* data) {
    Drumgen* self = (Drumgen*)instance;
    switch (port) {
        case 0: self->midi_out = (LV2_Atom_Sequence*)data; break;
        case 1: self->bpm_port = (float*)data; break;
        case 2: self->style_port = (float*)data; break;
        case 3: self->density_port = (float*)data; break;
        case 4: self->measures_port = (float*)data; break;
        case 5: self->division_port = (float*)data; break;
        case 6: self->note_kick = (float*)data; break;
        case 7: self->note_snare = (float*)data; break;
        case 8: self->note_hat = (float*)data; break;
        case 9: self->note_open = (float*)data; break;
        case 10: self->note_clap = (float*)data; break;
        case 11: self->note_tom_hi = (float*)data; break;
        case 12: self->note_tom_lo = (float*)data; break;
        case 13: self->note_crash = (float*)data; break;
        case 14: self->note_ride = (float*)data; break;
        case 15: self->note_rim = (float*)data; break;
        case 16: self->progress_port = (float*)data; break;
        case 17: self->humanize_port = (float*)data; break;
        case 18: self->swing_port = (float*)data; break;
        case 19: self->mute_kick = (float*)data; break;
        case 20: self->mute_snare = (float*)data; break;
        case 21: self->mute_hats = (float*)data; break;
        case 22: self->mute_percs = (float*)data; break;
    }
}

static void send_note(Drumgen* self, int note, uint8_t vel) {
    if (note < 0 || note > 127) return;
    
    // --- HUMANIZE ---
    float hum = *(self->humanize_port);
    if (hum > 0.0f) {
        int range = (int)(hum * 0.3f); 
        int var = (rand() % (range * 2 + 1)) - range;
        int new_vel = (int)vel + var;
        if (new_vel < 1) new_vel = 1;
        if (new_vel > 127) new_vel = 127;
        vel = (uint8_t)new_vel;
    }

    lv2_atom_forge_frame_time(&self->forge, 0); 
    uint8_t msg[3] = { 0x90, (uint8_t)note, vel };
    lv2_atom_forge_atom(&self->forge, 3, self->midi_MidiEvent);
    lv2_atom_forge_raw(&self->forge, msg, 3);
    lv2_atom_forge_pad(&self->forge, 3);
}

static void run(LV2_Handle instance, uint32_t n_samples) {
    Drumgen* self = (Drumgen*)instance;
    int port_style = (int)*(self->style_port);
    int d = (int)*(self->density_port);
    int m = (int)*(self->measures_port);
    
    if (port_style != self->current_playing_style && self->requested_style != port_style) { 
        self->requested_style = port_style; 
        self->change_pending = true; 
    }
    if (d != self->last_density || m != self->last_measures || self->current_playing_style == -1) {
        if (self->current_playing_style == -1) self->current_playing_style = port_style;
        build_pattern(self, self->current_playing_style);
        self->last_density = d; self->last_measures = m;
        if (self->current_step >= self->total_steps) self->current_step = 0;
    }

    float bpm = *(self->bpm_port); 
    if (bpm < 1.0f) bpm = 120.0f;
    
    int div_setting = (int)*(self->division_port);
    double divider = 1.0; 
    if (div_setting == 2) divider = 2.0; 
    if (div_setting == 3) divider = 4.0;
    
    // --- SWING ---
    float swing = *(self->swing_port);
    double swing_factor = 0.0;
    if (swing > 0.0f) {
        swing_factor = (swing / 100.0f) * 0.33f; 
    }

    double base_samples = (60.0 / bpm / 4.0) * self->rate * divider;
    double samples_per_step = base_samples;

    if (self->current_step % 2 == 0) {
        samples_per_step = base_samples * (1.0 + swing_factor);
    } else {
        samples_per_step = base_samples * (1.0 - swing_factor);
    }

    const uint32_t capacity = self->midi_out->atom.size;
    lv2_atom_forge_set_buffer(&self->forge, (uint8_t*)self->midi_out, capacity);
    lv2_atom_forge_sequence_head(&self->forge, &self->frame, 0);

    self->sample_counter += n_samples;
    if (self->sample_counter >= samples_per_step) {
        self->sample_counter -= samples_per_step;
        
        if (self->progress_port) *(self->progress_port) = ((float)self->current_step / (float)self->total_steps) * 100.0f;

        if (self->current_step == 0 && self->change_pending) {
            self->current_playing_style = self->requested_style;
            build_pattern(self, self->current_playing_style);
            self->change_pending = false;
            // Check Mute Percs
            if (d > 30 && self->current_playing_style < TEST_KICK && !is_house_family(self->current_playing_style) && (*self->mute_percs < 0.5)) 
                send_note(self, (int)*(self->note_crash), 100);
        }

        uint16_t mask = self->pattern[self->current_step];
        
        // --- MUTES ---
        if (*(self->mute_kick) > 0.5) mask &= ~M_KICK;
        if (*(self->mute_snare) > 0.5) mask &= ~(M_SNARE | M_CLAP | M_RIM);
        if (*(self->mute_hats) > 0.5) mask &= ~(M_HAT | M_OPEN | M_HAT_PEDAL);
        if (*(self->mute_percs) > 0.5) mask &= ~(M_TOM_HI | M_TOM_LO | M_CRASH | M_RIDE | M_RIDE_BELL);

        if (mask & M_KICK)      send_note(self, (int)*(self->note_kick), 100);
        if (mask & M_SNARE)     send_note(self, (int)*(self->note_snare), 100);
        if (mask & M_HAT)       send_note(self, (int)*(self->note_hat), 80);
        if (mask & M_OPEN)      send_note(self, (int)*(self->note_open), 80);
        if (mask & M_TOM_HI)    send_note(self, (int)*(self->note_tom_hi), 90);
        if (mask & M_TOM_LO)    send_note(self, (int)*(self->note_tom_lo), 90);
        if (mask & M_CRASH)     send_note(self, (int)*(self->note_crash), 100);
        if (mask & M_RIDE)      send_note(self, (int)*(self->note_ride), 80);
        if (mask & M_CLAP)      send_note(self, (int)*(self->note_clap), 90);
        if (mask & M_RIM)       send_note(self, (int)*(self->note_rim), 90);
        if (mask & M_HAT_PEDAL) send_note(self, (int)*(self->note_hat), 60); 
        if (mask & M_RIDE_BELL) send_note(self, (int)*(self->note_ride), 110); 

        // Crash Début Boucle + Check Mute
        if (self->current_step == 0 && d > 80 && m > 1 && !self->change_pending && self->current_playing_style < TEST_KICK && (*self->mute_percs < 0.5)) {
             send_note(self, (int)*(self->note_crash), 100);
        }

        self->current_step++;
        if (self->current_step >= self->total_steps) self->current_step = 0;
    }
}
static void cleanup(LV2_Handle instance) { free(instance); }
static const LV2_Descriptor descriptor = { DRUMGEN_URI, instantiate, connect_port, NULL, run, NULL, cleanup };
LV2_SYMBOL_EXPORT const LV2_Descriptor* lv2_descriptor(uint32_t index) { return index == 0 ? &descriptor : NULL; }