// padthv1_lv2.cpp
//
/****************************************************************************
   Copyright (C) 2012-2017, rncbc aka Rui Nuno Capela. All rights reserved.

   This program is free software; you can redistribute it and/or
   modify it under the terms of the GNU General Public License
   as published by the Free Software Foundation; either version 2
   of the License, or (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License along
   with this program; if not, write to the Free Software Foundation, Inc.,
   51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.

*****************************************************************************/

#include "padthv1_lv2.h"
#include "padthv1_config.h"
#include "padthv1_sched.h"

#include "padthv1_programs.h"
#include "padthv1_controls.h"

#include "lv2/lv2plug.in/ns/ext/midi/midi.h"
#include "lv2/lv2plug.in/ns/ext/time/time.h"
#include "lv2/lv2plug.in/ns/ext/atom/util.h"

#include "lv2/lv2plug.in/ns/ext/state/state.h"

#include "lv2/lv2plug.in/ns/ext/options/options.h"
#include "lv2/lv2plug.in/ns/ext/buf-size/buf-size.h"

#ifndef CONFIG_LV2_ATOM_FORGE_OBJECT
#define lv2_atom_forge_object(forge, frame, id, otype) \
		lv2_atom_forge_blank(forge, frame, id, otype)
#endif

#ifndef CONFIG_LV2_ATOM_FORGE_KEY
#define lv2_atom_forge_key(forge, key) \
		lv2_atom_forge_property_head(forge, key, 0)
#endif

#ifndef LV2_STATE__StateChanged
#define LV2_STATE__StateChanged LV2_STATE_PREFIX "StateChanged"
#endif

#include <stdlib.h>
#include <math.h>

#include <QDomDocument>


//-------------------------------------------------------------------------
// padthv1_lv2 - impl.
//

// atom-like message used internally with worker/schedule
typedef struct {
	LV2_Atom atom;
} padthv1_lv2_worker_message;


padthv1_lv2::padthv1_lv2 (
	double sample_rate, const LV2_Feature *const *host_features )
	: padthv1(2, float(sample_rate))
{
	::memset(&m_urids, 0, sizeof(m_urids));

	m_urid_map = NULL;
	m_atom_in  = NULL;
	m_atom_out = NULL;
	m_schedule = NULL;
	m_ndelta   = 0;

	const LV2_Options_Option *host_options = NULL;

	for (int i = 0; host_features && host_features[i]; ++i) {
		const LV2_Feature *host_feature = host_features[i];
		if (::strcmp(host_feature->URI, LV2_URID_MAP_URI) == 0) {
			m_urid_map = (LV2_URID_Map *) host_feature->data;
			if (m_urid_map) {
 				m_urids.gen1_sample = m_urid_map->map(
 					m_urid_map->handle, PADTHV1_LV2_PREFIX "GEN1_SAMPLE");
 				m_urids.gen1_loop_start = m_urid_map->map(
 					m_urid_map->handle, PADTHV1_LV2_PREFIX "GEN1_LOOP_START");
 				m_urids.gen1_loop_end = m_urid_map->map(
 					m_urid_map->handle, PADTHV1_LV2_PREFIX "GEN1_LOOP_END");
				m_urids.gen1_update = m_urid_map->map(
					m_urid_map->handle, PADTHV1_LV2_PREFIX "GEN1_UPDATE");
				m_urids.atom_Blank = m_urid_map->map(
					m_urid_map->handle, LV2_ATOM__Blank);
				m_urids.atom_Object = m_urid_map->map(
					m_urid_map->handle, LV2_ATOM__Object);
				m_urids.atom_Float = m_urid_map->map(
					m_urid_map->handle, LV2_ATOM__Float);
				m_urids.atom_Int = m_urid_map->map(
					m_urid_map->handle, LV2_ATOM__Int);
				m_urids.atom_Path = m_urid_map->map(
					m_urid_map->handle, LV2_ATOM__Path);
				m_urids.time_Position = m_urid_map->map(
					m_urid_map->handle, LV2_TIME__Position);
				m_urids.time_beatsPerMinute = m_urid_map->map(
					m_urid_map->handle, LV2_TIME__beatsPerMinute);
				m_urids.midi_MidiEvent = m_urid_map->map(
					m_urid_map->handle, LV2_MIDI__MidiEvent);
 				m_urids.midi_MidiEvent = m_urid_map->map(
 					m_urid_map->handle, LV2_MIDI__MidiEvent);
				m_urids.bufsz_minBlockLength = m_urid_map->map(
					m_urid_map->handle, LV2_BUF_SIZE__minBlockLength);
 				m_urids.bufsz_maxBlockLength = m_urid_map->map(
 					m_urid_map->handle, LV2_BUF_SIZE__maxBlockLength);
			#ifdef LV2_BUF_SIZE__nominalBlockLength
				m_urids.bufsz_nominalBlockLength = m_urid_map->map(
					m_urid_map->handle, LV2_BUF_SIZE__nominalBlockLength);
			#endif
				m_urids.state_StateChanged = m_urid_map->map(
					m_urid_map->handle, LV2_STATE__StateChanged);
			}
		}
		else
		if (::strcmp(host_feature->URI, LV2_WORKER__schedule) == 0)
			m_schedule = (LV2_Worker_Schedule *) host_feature->data;
		else
		if (::strcmp(host_feature->URI, LV2_OPTIONS__options) == 0)
			host_options = (const LV2_Options_Option *) host_feature->data;
	}

	uint32_t buffer_size = 0; // whatever happened to safe default?

	for (int i = 0; host_options && host_options[i].key; ++i) {
		const LV2_Options_Option *host_option = &host_options[i];
		if (host_option->type == m_urids.atom_Int) {
			uint32_t block_length = 0;
			if (host_option->key == m_urids.bufsz_minBlockLength)
				block_length = *(int *) host_option->value;
			else
			if (host_option->key == m_urids.bufsz_maxBlockLength)
				block_length = *(int *) host_option->value;
		#ifdef LV2_BUF_SIZE__nominalBlockLength
			else
			if (host_option->key == m_urids.bufsz_nominalBlockLength)
				block_length = *(int *) host_option->value;
		#endif
			// choose the lengthier...
			if (buffer_size < block_length)
				buffer_size = block_length;
		}
 	}
 
	padthv1::setBufferSize(buffer_size);

	lv2_atom_forge_init(&m_forge, m_urid_map);

	const uint16_t nchannels = padthv1::channels();
	m_ins  = new float * [nchannels];
	m_outs = new float * [nchannels];
	for (uint16_t k = 0; k < nchannels; ++k)
		m_ins[k] = m_outs[k] = NULL;
}


padthv1_lv2::~padthv1_lv2 (void)
{
	delete [] m_outs;
	delete [] m_ins;
}


void padthv1_lv2::connect_port ( uint32_t port, void *data )
{
	switch(PortIndex(port)) {
	case MidiIn:
		m_atom_in = (LV2_Atom_Sequence *) data;
		break;
	case Notify:
		m_atom_out = (LV2_Atom_Sequence *) data;
		break;
	case AudioInL:
		m_ins[0] = (float *) data;
		break;
	case AudioInR:
		m_ins[1] = (float *) data;
		break;
	case AudioOutL:
		m_outs[0] = (float *) data;
		break;
	case AudioOutR:
		m_outs[1] = (float *) data;
		break;
	default:
		padthv1::setParamPort(padthv1::ParamIndex(port - ParamBase), (float *) data);
		break;
	}
}


void padthv1_lv2::run ( uint32_t nframes )
{
	const uint16_t nchannels = padthv1::channels();
	float *ins[nchannels], *outs[nchannels];
	for (uint16_t k = 0; k < nchannels; ++k) {
		ins[k]  = m_ins[k];
		outs[k] = m_outs[k];
	}

	if (m_atom_out) {
		const uint32_t capacity = m_atom_out->atom.size;
		lv2_atom_forge_set_buffer(&m_forge, (uint8_t *) m_atom_out, capacity);
		lv2_atom_forge_sequence_head(&m_forge, &m_notify_frame, 0);
	}

	uint32_t ndelta = 0;

	if (m_atom_in) {
		LV2_ATOM_SEQUENCE_FOREACH(m_atom_in, event) {
			if (event == NULL)
				continue;
			if (event->body.type == m_urids.midi_MidiEvent) {
				uint8_t *data = (uint8_t *) LV2_ATOM_BODY(&event->body);
				if (event->time.frames > ndelta) {
					const uint32_t nread = event->time.frames - ndelta;
					if (nread > 0) {
						padthv1::process(ins, outs, nread);
						for (uint16_t k = 0; k < nchannels; ++k) {
							ins[k]  += nread;
							outs[k] += nread;
						}
					}
				}
				ndelta = event->time.frames;
				padthv1::process_midi(data, event->body.size);
			}
			else
			if (event->body.type == m_urids.atom_Blank ||
				event->body.type == m_urids.atom_Object) {
				const LV2_Atom_Object *object
					= (LV2_Atom_Object *) &event->body;
				if (object->body.otype == m_urids.time_Position) {
					LV2_Atom *atom = NULL;
					lv2_atom_object_get(object,
						m_urids.time_beatsPerMinute, &atom, NULL);
					if (atom && atom->type == m_urids.atom_Float) {
						const float host_bpm = ((LV2_Atom_Float *) atom)->body;
						if (::fabsf(host_bpm - padthv1::tempo()) > 0.001f)
							padthv1::setTempo(host_bpm);
					}
				}
			}
		}
		// remember last time for worker response
		m_ndelta = ndelta;
	//	m_atom_in = NULL;
	}

	if (nframes > ndelta)
		padthv1::process(ins, outs, nframes - ndelta);
}


void padthv1_lv2::activate (void)
{
	padthv1::reset();
}


void padthv1_lv2::deactivate (void)
{
	padthv1::reset();
}


uint32_t padthv1_lv2::urid_map ( const char *uri ) const
{
	return (m_urid_map ? m_urid_map->map(m_urid_map->handle, uri) : 0);
}


//-------------------------------------------------------------------------
// padthv1_lv2 - LV2 State interface.
//

static LV2_State_Status padthv1_lv2_state_save ( LV2_Handle instance,
	LV2_State_Store_Function store, LV2_State_Handle handle,
	uint32_t flags, const LV2_Feature *const * /*features*/ )
{
	padthv1_lv2 *pPlugin = static_cast<padthv1_lv2 *> (instance);
	if (pPlugin == NULL)
		return LV2_STATE_ERR_UNKNOWN;

	const uint32_t key = pPlugin->urid_map(PADTHV1_LV2_PREFIX "state");
	if (key == 0)
		return LV2_STATE_ERR_NO_PROPERTY;

	const uint32_t type = pPlugin->urid_map(LV2_ATOM__Chunk);
	if (type == 0)
		return LV2_STATE_ERR_BAD_TYPE;
#if 0
	if ((flags & (LV2_STATE_IS_POD | LV2_STATE_IS_PORTABLE)) == 0)
		return LV2_STATE_ERR_BAD_FLAGS;
#else
	flags |= (LV2_STATE_IS_POD | LV2_STATE_IS_PORTABLE);
#endif

	QDomDocument doc(PADTHV1_TITLE);
	QDomElement eSamples = doc.createElement("samples");
	padthv1_param::saveSamples(pPlugin, doc, eSamples);
	doc.appendChild(eSamples);

	const QByteArray data(doc.toByteArray());
	const char *value = data.constData();
	size_t size = data.size();

	return (*store)(handle, key, value, size, type, flags);
}


static LV2_State_Status padthv1_lv2_state_restore ( LV2_Handle instance,
	LV2_State_Retrieve_Function retrieve, LV2_State_Handle handle,
	uint32_t flags, const LV2_Feature *const * /*features*/ )
{
	padthv1_lv2 *pPlugin = static_cast<padthv1_lv2 *> (instance);
	if (pPlugin == NULL)
		return LV2_STATE_ERR_UNKNOWN;

	const uint32_t key = pPlugin->urid_map(PADTHV1_LV2_PREFIX "state");
	if (key == 0)
		return LV2_STATE_ERR_NO_PROPERTY;

	const uint32_t chunk_type = pPlugin->urid_map(LV2_ATOM__Chunk);
	if (chunk_type == 0)
		return LV2_STATE_ERR_BAD_TYPE;

	size_t size = 0;
	uint32_t type = 0;
//	flags = 0;

	const char *value
		= (const char *) (*retrieve)(handle, key, &size, &type, &flags);

	if (size < 2)
		return LV2_STATE_ERR_UNKNOWN;

	if (type != chunk_type)
		return LV2_STATE_ERR_BAD_TYPE;

	if ((flags & (LV2_STATE_IS_POD | LV2_STATE_IS_PORTABLE)) == 0)
		return LV2_STATE_ERR_BAD_FLAGS;

	if (value == NULL)
		return LV2_STATE_ERR_UNKNOWN;

	QDomDocument doc(PADTHV1_TITLE);
	if (doc.setContent(QByteArray(value, size))) {
		QDomElement eSamples = doc.documentElement();
		if (eSamples.tagName() == "samples")
			padthv1_param::loadSamples(pPlugin, eSamples);
	}

	pPlugin->reset();

	padthv1_sched::sync_notify(pPlugin, padthv1_sched::Sample, 3);

	return LV2_STATE_SUCCESS;
}


static const LV2_State_Interface padthv1_lv2_state_interface =
{
	padthv1_lv2_state_save,
	padthv1_lv2_state_restore
};


#ifdef CONFIG_LV2_PROGRAMS

#include "padthv1_programs.h"

const LV2_Program_Descriptor *padthv1_lv2::get_program ( uint32_t index )
{
	padthv1_programs *pPrograms = padthv1::programs();
	const padthv1_programs::Banks& banks = pPrograms->banks();
	padthv1_programs::Banks::ConstIterator bank_iter = banks.constBegin();
	const padthv1_programs::Banks::ConstIterator& bank_end = banks.constEnd();
	for (uint32_t i = 0; bank_iter != bank_end; ++bank_iter) {
		padthv1_programs::Bank *pBank = bank_iter.value();
		const padthv1_programs::Progs& progs = pBank->progs();
		padthv1_programs::Progs::ConstIterator prog_iter = progs.constBegin();
		const padthv1_programs::Progs::ConstIterator& prog_end = progs.constEnd();
		for ( ; prog_iter != prog_end; ++prog_iter, ++i) {
			padthv1_programs::Prog *pProg = prog_iter.value();
			if (i >= index) {
				m_aProgramName = pProg->name().toUtf8();
				m_program.bank = pBank->id();
				m_program.program = pProg->id();
				m_program.name = m_aProgramName.constData();
				return &m_program;
			}
		}
	}

	return NULL;
}

void padthv1_lv2::select_program ( uint32_t bank, uint32_t program )
{
	padthv1::programs()->select_program(bank, program);
}

#endif	// CONFIG_LV2_PROGRAMS


void padthv1_lv2::updatePreset ( bool /*bDirty*/ )
{
	if (m_schedule /*&& bDirty*/) {
		padthv1_lv2_worker_message mesg;
		mesg.atom.type = m_urids.state_StateChanged;
		mesg.atom.size = 0; // nothing else matters.
		m_schedule->schedule_work(
			m_schedule->handle, sizeof(mesg), &mesg);
	}
}


bool padthv1_lv2::worker_work ( const void *data, uint32_t size )
{
	if (size != sizeof(padthv1_lv2_worker_message))
		return false;

	const padthv1_lv2_worker_message *mesg
		= (const padthv1_lv2_worker_message *) data;

	return (mesg->atom.type == m_urids.state_StateChanged);
}


bool padthv1_lv2::worker_response ( const void *data, uint32_t size )
{
	if (size != sizeof(padthv1_lv2_worker_message))
		return false;

	const padthv1_lv2_worker_message *mesg
		= (const padthv1_lv2_worker_message *) data;

	if (mesg->atom.type == m_urids.state_StateChanged)
		return state_changed();
	else
		return false;
}


bool padthv1_lv2::state_changed (void)
{
	lv2_atom_forge_frame_time(&m_forge, m_ndelta);

	LV2_Atom_Forge_Frame frame;
	lv2_atom_forge_object(&m_forge, &frame, 0, m_urids.state_StateChanged);
	lv2_atom_forge_pop(&m_forge, &frame);

	return true;
}


//-------------------------------------------------------------------------
// padthv1_lv2 - LV2 desc.
//

static LV2_Handle padthv1_lv2_instantiate (
	const LV2_Descriptor *, double sample_rate, const char *,
	const LV2_Feature *const *host_features )
{
	return new padthv1_lv2(sample_rate, host_features);
}


static void padthv1_lv2_connect_port (
	LV2_Handle instance, uint32_t port, void *data )
{
	padthv1_lv2 *pPlugin = static_cast<padthv1_lv2 *> (instance);
	if (pPlugin)
		pPlugin->connect_port(port, data);
}


static void padthv1_lv2_run ( LV2_Handle instance, uint32_t nframes )
{
	padthv1_lv2 *pPlugin = static_cast<padthv1_lv2 *> (instance);
	if (pPlugin)
		pPlugin->run(nframes);
}


static void padthv1_lv2_activate ( LV2_Handle instance )
{
	padthv1_lv2 *pPlugin = static_cast<padthv1_lv2 *> (instance);
	if (pPlugin)
		pPlugin->activate();
}


static void padthv1_lv2_deactivate ( LV2_Handle instance )
{
	padthv1_lv2 *pPlugin = static_cast<padthv1_lv2 *> (instance);
	if (pPlugin)
		pPlugin->deactivate();
}


static void padthv1_lv2_cleanup ( LV2_Handle instance )
{
	padthv1_lv2 *pPlugin = static_cast<padthv1_lv2 *> (instance);
	if (pPlugin)
		delete pPlugin;
}


#ifdef CONFIG_LV2_PROGRAMS

static const LV2_Program_Descriptor *padthv1_lv2_programs_get_program (
	LV2_Handle instance, uint32_t index )
{
	padthv1_lv2 *pPlugin = static_cast<padthv1_lv2 *> (instance);
	if (pPlugin)
		return pPlugin->get_program(index);
	else
		return NULL;
}

static void padthv1_lv2_programs_select_program (
	LV2_Handle instance, uint32_t bank, uint32_t program )
{
	padthv1_lv2 *pPlugin = static_cast<padthv1_lv2 *> (instance);
	if (pPlugin)
		pPlugin->select_program(bank, program);
}

static const LV2_Programs_Interface padthv1_lv2_programs_interface =
{
	padthv1_lv2_programs_get_program,
	padthv1_lv2_programs_select_program,
};

#endif	// CONFIG_LV2_PROGRAMS


static LV2_Worker_Status padthv1_lv2_worker_work (
	LV2_Handle instance, LV2_Worker_Respond_Function respond,
	LV2_Worker_Respond_Handle handle, uint32_t size, const void *data )
{
	padthv1_lv2 *pSynth = static_cast<padthv1_lv2 *> (instance);
	if (pSynth && pSynth->worker_work(data, size)) {
		respond(handle, size, data);
		return LV2_WORKER_SUCCESS;
	}

	return LV2_WORKER_ERR_UNKNOWN;
}


static LV2_Worker_Status padthv1_lv2_worker_response (
	LV2_Handle instance, uint32_t size, const void *data )
{
	padthv1_lv2 *pSynth = static_cast<padthv1_lv2 *> (instance);
	if (pSynth && pSynth->worker_response(data, size))
		return LV2_WORKER_SUCCESS;
	else
		return LV2_WORKER_ERR_UNKNOWN;
}


static const LV2_Worker_Interface padthv1_lv2_worker_interface =
{
	padthv1_lv2_worker_work,
	padthv1_lv2_worker_response,
	NULL
};


static const void *padthv1_lv2_extension_data ( const char *uri )
{
#ifdef CONFIG_LV2_PROGRAMS
	if (::strcmp(uri, LV2_PROGRAMS__Interface) == 0)
		return &padthv1_lv2_programs_interface;
	else
#endif
	if (::strcmp(uri, LV2_WORKER__interface) == 0)
		return &padthv1_lv2_worker_interface;
	else
	if (::strcmp(uri, LV2_STATE__interface) == 0)
		return &padthv1_lv2_state_interface;

	return NULL;
}


static const LV2_Descriptor padthv1_lv2_descriptor =
{
	PADTHV1_LV2_URI,
	padthv1_lv2_instantiate,
	padthv1_lv2_connect_port,
	padthv1_lv2_activate,
	padthv1_lv2_run,
	padthv1_lv2_deactivate,
	padthv1_lv2_cleanup,
	padthv1_lv2_extension_data
};


LV2_SYMBOL_EXPORT const LV2_Descriptor *lv2_descriptor ( uint32_t index )
{
	return (index == 0 ? &padthv1_lv2_descriptor : NULL);
}


// end of padthv1_lv2.cpp

