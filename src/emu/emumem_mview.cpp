// license:BSD-3-Clause
// copyright-holders:Aaron Giles,Olivier Galibert
/***************************************************************************

    emumem.cpp

    Functions which handle device memory access.
    Memory view specific functions

***************************************************************************/

#include "emu.h"
#include <list>
#include <map>
#include "emuopts.h"
#include "debug/debugcpu.h"

#include "emumem_mud.h"
#include "emumem_hea.h"
#include "emumem_hem.h"
#include "emumem_hedp.h"
#include "emumem_heun.h"
#include "emumem_heu.h"
#include "emumem_hedr.h"
#include "emumem_hedw.h"
#include "emumem_hep.h"
#include "emumem_het.h"

#define VERBOSE 0

#if VERBOSE
template <typename Format, typename... Params> static void VPRINTF(Format &&fmt, Params &&...args)
{
	util::stream_format(std::cerr, std::forward<Format>(fmt), std::forward<Params>(args)...);
}
#else
template <typename Format, typename... Params> static void VPRINTF(Format &&, Params &&...) {}
#endif

namespace {

template <typename Delegate> struct handler_width;
template <> struct handler_width<read8_delegate> { static constexpr int value = 0; };
template <> struct handler_width<read8m_delegate> { static constexpr int value = 0; };
template <> struct handler_width<read8s_delegate> { static constexpr int value = 0; };
template <> struct handler_width<read8sm_delegate> { static constexpr int value = 0; };
template <> struct handler_width<read8mo_delegate> { static constexpr int value = 0; };
template <> struct handler_width<read8smo_delegate> { static constexpr int value = 0; };
template <> struct handler_width<write8_delegate> { static constexpr int value = 0; };
template <> struct handler_width<write8m_delegate> { static constexpr int value = 0; };
template <> struct handler_width<write8s_delegate> { static constexpr int value = 0; };
template <> struct handler_width<write8sm_delegate> { static constexpr int value = 0; };
template <> struct handler_width<write8mo_delegate> { static constexpr int value = 0; };
template <> struct handler_width<write8smo_delegate> { static constexpr int value = 0; };
template <> struct handler_width<read16_delegate> { static constexpr int value = 1; };
template <> struct handler_width<read16m_delegate> { static constexpr int value = 1; };
template <> struct handler_width<read16s_delegate> { static constexpr int value = 1; };
template <> struct handler_width<read16sm_delegate> { static constexpr int value = 1; };
template <> struct handler_width<read16mo_delegate> { static constexpr int value = 1; };
template <> struct handler_width<read16smo_delegate> { static constexpr int value = 1; };
template <> struct handler_width<write16_delegate> { static constexpr int value = 1; };
template <> struct handler_width<write16m_delegate> { static constexpr int value = 1; };
template <> struct handler_width<write16s_delegate> { static constexpr int value = 1; };
template <> struct handler_width<write16sm_delegate> { static constexpr int value = 1; };
template <> struct handler_width<write16mo_delegate> { static constexpr int value = 1; };
template <> struct handler_width<write16smo_delegate> { static constexpr int value = 1; };
template <> struct handler_width<read32_delegate> { static constexpr int value = 2; };
template <> struct handler_width<read32m_delegate> { static constexpr int value = 2; };
template <> struct handler_width<read32s_delegate> { static constexpr int value = 2; };
template <> struct handler_width<read32sm_delegate> { static constexpr int value = 2; };
template <> struct handler_width<read32mo_delegate> { static constexpr int value = 2; };
template <> struct handler_width<read32smo_delegate> { static constexpr int value = 2; };
template <> struct handler_width<write32_delegate> { static constexpr int value = 2; };
template <> struct handler_width<write32m_delegate> { static constexpr int value = 2; };
template <> struct handler_width<write32s_delegate> { static constexpr int value = 2; };
template <> struct handler_width<write32sm_delegate> { static constexpr int value = 2; };
template <> struct handler_width<write32mo_delegate> { static constexpr int value = 2; };
template <> struct handler_width<write32smo_delegate> { static constexpr int value = 2; };
template <> struct handler_width<read64_delegate> { static constexpr int value = 3; };
template <> struct handler_width<read64m_delegate> { static constexpr int value = 3; };
template <> struct handler_width<read64s_delegate> { static constexpr int value = 3; };
template <> struct handler_width<read64sm_delegate> { static constexpr int value = 3; };
template <> struct handler_width<read64mo_delegate> { static constexpr int value = 3; };
template <> struct handler_width<read64smo_delegate> { static constexpr int value = 3; };
template <> struct handler_width<write64_delegate> { static constexpr int value = 3; };
template <> struct handler_width<write64m_delegate> { static constexpr int value = 3; };
template <> struct handler_width<write64s_delegate> { static constexpr int value = 3; };
template <> struct handler_width<write64sm_delegate> { static constexpr int value = 3; };
template <> struct handler_width<write64mo_delegate> { static constexpr int value = 3; };
template <> struct handler_width<write64smo_delegate> { static constexpr int value = 3; };
} // anonymous namespace

address_map_entry &memory_view::memory_view_entry::operator()(offs_t start, offs_t end)
{
	return (*m_map)(start, end);
}

template<int Level, int Width, int AddrShift, endianness_t Endian>
class memory_view_entry_specific : public memory_view::memory_view_entry
{
	using uX = typename emu::detail::handler_entry_size<Width>::uX;
	using NativeType = uX;

	// constants describing the native size
	static constexpr u32 NATIVE_BYTES = 1 << Width;
	static constexpr u32 NATIVE_STEP = AddrShift >= 0 ? NATIVE_BYTES << iabs(AddrShift) : NATIVE_BYTES >> iabs(AddrShift);
	static constexpr u32 NATIVE_MASK = NATIVE_STEP - 1;
	static constexpr u32 NATIVE_BITS = 8 * NATIVE_BYTES;

	static constexpr offs_t offset_to_byte(offs_t offset) { return AddrShift < 0 ? offset << iabs(AddrShift) : offset >> iabs(AddrShift); }

public:
	memory_view_entry_specific(const address_space_config &config, memory_manager &manager, memory_view &view, int id) : memory_view_entry(config, manager, view, id) {
	}

	virtual ~memory_view_entry_specific() = default;

	handler_entry_read <Width, AddrShift, Endian> *r() { return static_cast<handler_entry_read <Width, AddrShift, Endian> *>(m_view.m_handler_read); }
	handler_entry_write<Width, AddrShift, Endian> *w() { return static_cast<handler_entry_write<Width, AddrShift, Endian> *>(m_view.m_handler_write); }

	void invalidate_caches(read_or_write readorwrite) { return m_view.m_space->invalidate_caches(readorwrite); }

	virtual void populate_from_map(address_map *map = nullptr) override;

	using address_space_installer::install_read_tap;
	using address_space_installer::install_write_tap;
	using address_space_installer::install_readwrite_tap;

	virtual memory_passthrough_handler *install_read_tap(offs_t addrstart, offs_t addrend, offs_t addrmirror, std::string name, std::function<void (offs_t offset, uX &data, uX mem_mask)> tap, memory_passthrough_handler *mph) override;
	virtual memory_passthrough_handler *install_write_tap(offs_t addrstart, offs_t addrend, offs_t addrmirror, std::string name, std::function<void (offs_t offset, uX &data, uX mem_mask)> tap, memory_passthrough_handler *mph) override;
	virtual memory_passthrough_handler *install_readwrite_tap(offs_t addrstart, offs_t addrend, offs_t addrmirror, std::string name, std::function<void (offs_t offset, uX &data, uX mem_mask)> tapr, std::function<void (offs_t offset, uX &data, uX mem_mask)> tapw, memory_passthrough_handler *mph) override;

	virtual void unmap_generic(offs_t addrstart, offs_t addrend, offs_t addrmirror, read_or_write readorwrite, bool quiet) override;
	virtual void install_ram_generic(offs_t addrstart, offs_t addrend, offs_t addrmirror, read_or_write readorwrite, void *baseptr) override;
	virtual void install_bank_generic(offs_t addrstart, offs_t addrend, offs_t addrmirror, memory_bank *rbank, memory_bank *wbank) override;
	virtual void install_view(offs_t addrstart, offs_t addrend, offs_t addrmirror, memory_view &view) override;
	virtual void install_readwrite_port(offs_t addrstart, offs_t addrend, offs_t addrmirror, std::string rtag, std::string wtag) override;
	virtual void install_device_delegate(offs_t addrstart, offs_t addrend, device_t &device, address_map_constructor &map, u64 unitmask, int cswidth) override;


	void install_read_handler(offs_t addrstart, offs_t addrend, offs_t addrmask, offs_t addrmirror, offs_t addrselect, read8_delegate rhandler, u64 unitmask = 0, int cswidth = 0) override
	{ install_read_handler_impl(addrstart, addrend, addrmask, addrmirror, addrselect, unitmask, cswidth, rhandler); }
	void install_write_handler(offs_t addrstart, offs_t addrend, offs_t addrmask, offs_t addrmirror, offs_t addrselect, write8_delegate whandler, u64 unitmask = 0, int cswidth = 0) override
	{ install_write_handler_impl(addrstart, addrend, addrmask, addrmirror, addrselect, unitmask, cswidth, whandler); }
	void install_readwrite_handler(offs_t addrstart, offs_t addrend, offs_t addrmask, offs_t addrmirror, offs_t addrselect, read8_delegate rhandler, write8_delegate whandler, u64 unitmask = 0, int cswidth = 0) override
	{ install_readwrite_handler_impl(addrstart, addrend, addrmask, addrmirror, addrselect, unitmask, cswidth, rhandler, whandler); }
	void install_read_handler(offs_t addrstart, offs_t addrend, offs_t addrmask, offs_t addrmirror, offs_t addrselect, read16_delegate rhandler, u64 unitmask = 0, int cswidth = 0) override
	{ install_read_handler_impl(addrstart, addrend, addrmask, addrmirror, addrselect, unitmask, cswidth, rhandler); }
	void install_write_handler(offs_t addrstart, offs_t addrend, offs_t addrmask, offs_t addrmirror, offs_t addrselect, write16_delegate whandler, u64 unitmask = 0, int cswidth = 0) override
	{ install_write_handler_impl(addrstart, addrend, addrmask, addrmirror, addrselect, unitmask, cswidth, whandler); }
	void install_readwrite_handler(offs_t addrstart, offs_t addrend, offs_t addrmask, offs_t addrmirror, offs_t addrselect, read16_delegate rhandler, write16_delegate whandler, u64 unitmask = 0, int cswidth = 0) override
	{ install_readwrite_handler_impl(addrstart, addrend, addrmask, addrmirror, addrselect, unitmask, cswidth, rhandler, whandler); }
	void install_read_handler(offs_t addrstart, offs_t addrend, offs_t addrmask, offs_t addrmirror, offs_t addrselect, read32_delegate rhandler, u64 unitmask = 0, int cswidth = 0) override
	{ install_read_handler_impl(addrstart, addrend, addrmask, addrmirror, addrselect, unitmask, cswidth, rhandler); }
	void install_write_handler(offs_t addrstart, offs_t addrend, offs_t addrmask, offs_t addrmirror, offs_t addrselect, write32_delegate whandler, u64 unitmask = 0, int cswidth = 0) override
	{ install_write_handler_impl(addrstart, addrend, addrmask, addrmirror, addrselect, unitmask, cswidth, whandler); }
	void install_readwrite_handler(offs_t addrstart, offs_t addrend, offs_t addrmask, offs_t addrmirror, offs_t addrselect, read32_delegate rhandler, write32_delegate whandler, u64 unitmask = 0, int cswidth = 0) override
	{ install_readwrite_handler_impl(addrstart, addrend, addrmask, addrmirror, addrselect, unitmask, cswidth, rhandler, whandler); }
	void install_read_handler(offs_t addrstart, offs_t addrend, offs_t addrmask, offs_t addrmirror, offs_t addrselect, read64_delegate rhandler, u64 unitmask = 0, int cswidth = 0) override
	{ install_read_handler_impl(addrstart, addrend, addrmask, addrmirror, addrselect, unitmask, cswidth, rhandler); }
	void install_write_handler(offs_t addrstart, offs_t addrend, offs_t addrmask, offs_t addrmirror, offs_t addrselect, write64_delegate whandler, u64 unitmask = 0, int cswidth = 0) override
	{ install_write_handler_impl(addrstart, addrend, addrmask, addrmirror, addrselect, unitmask, cswidth, whandler); }
	void install_readwrite_handler(offs_t addrstart, offs_t addrend, offs_t addrmask, offs_t addrmirror, offs_t addrselect, read64_delegate rhandler, write64_delegate whandler, u64 unitmask = 0, int cswidth = 0) override
	{ install_readwrite_handler_impl(addrstart, addrend, addrmask, addrmirror, addrselect, unitmask, cswidth, rhandler, whandler); }

	void install_read_handler(offs_t addrstart, offs_t addrend, offs_t addrmask, offs_t addrmirror, offs_t addrselect, read8m_delegate rhandler, u64 unitmask = 0, int cswidth = 0) override
	{ install_read_handler_impl(addrstart, addrend, addrmask, addrmirror, addrselect, unitmask, cswidth, rhandler); }
	void install_write_handler(offs_t addrstart, offs_t addrend, offs_t addrmask, offs_t addrmirror, offs_t addrselect, write8m_delegate whandler, u64 unitmask = 0, int cswidth = 0) override
	{ install_write_handler_impl(addrstart, addrend, addrmask, addrmirror, addrselect, unitmask, cswidth, whandler); }
	void install_readwrite_handler(offs_t addrstart, offs_t addrend, offs_t addrmask, offs_t addrmirror, offs_t addrselect, read8m_delegate rhandler, write8m_delegate whandler, u64 unitmask = 0, int cswidth = 0) override
	{ install_readwrite_handler_impl(addrstart, addrend, addrmask, addrmirror, addrselect, unitmask, cswidth, rhandler, whandler); }
	void install_read_handler(offs_t addrstart, offs_t addrend, offs_t addrmask, offs_t addrmirror, offs_t addrselect, read16m_delegate rhandler, u64 unitmask = 0, int cswidth = 0) override
	{ install_read_handler_impl(addrstart, addrend, addrmask, addrmirror, addrselect, unitmask, cswidth, rhandler); }
	void install_write_handler(offs_t addrstart, offs_t addrend, offs_t addrmask, offs_t addrmirror, offs_t addrselect, write16m_delegate whandler, u64 unitmask = 0, int cswidth = 0) override
	{ install_write_handler_impl(addrstart, addrend, addrmask, addrmirror, addrselect, unitmask, cswidth, whandler); }
	void install_readwrite_handler(offs_t addrstart, offs_t addrend, offs_t addrmask, offs_t addrmirror, offs_t addrselect, read16m_delegate rhandler, write16m_delegate whandler, u64 unitmask = 0, int cswidth = 0) override
	{ install_readwrite_handler_impl(addrstart, addrend, addrmask, addrmirror, addrselect, unitmask, cswidth, rhandler, whandler); }
	void install_read_handler(offs_t addrstart, offs_t addrend, offs_t addrmask, offs_t addrmirror, offs_t addrselect, read32m_delegate rhandler, u64 unitmask = 0, int cswidth = 0) override
	{ install_read_handler_impl(addrstart, addrend, addrmask, addrmirror, addrselect, unitmask, cswidth, rhandler); }
	void install_write_handler(offs_t addrstart, offs_t addrend, offs_t addrmask, offs_t addrmirror, offs_t addrselect, write32m_delegate whandler, u64 unitmask = 0, int cswidth = 0) override
	{ install_write_handler_impl(addrstart, addrend, addrmask, addrmirror, addrselect, unitmask, cswidth, whandler); }
	void install_readwrite_handler(offs_t addrstart, offs_t addrend, offs_t addrmask, offs_t addrmirror, offs_t addrselect, read32m_delegate rhandler, write32m_delegate whandler, u64 unitmask = 0, int cswidth = 0) override
	{ install_readwrite_handler_impl(addrstart, addrend, addrmask, addrmirror, addrselect, unitmask, cswidth, rhandler, whandler); }
	void install_read_handler(offs_t addrstart, offs_t addrend, offs_t addrmask, offs_t addrmirror, offs_t addrselect, read64m_delegate rhandler, u64 unitmask = 0, int cswidth = 0) override
	{ install_read_handler_impl(addrstart, addrend, addrmask, addrmirror, addrselect, unitmask, cswidth, rhandler); }
	void install_write_handler(offs_t addrstart, offs_t addrend, offs_t addrmask, offs_t addrmirror, offs_t addrselect, write64m_delegate whandler, u64 unitmask = 0, int cswidth = 0) override
	{ install_write_handler_impl(addrstart, addrend, addrmask, addrmirror, addrselect, unitmask, cswidth, whandler); }
	void install_readwrite_handler(offs_t addrstart, offs_t addrend, offs_t addrmask, offs_t addrmirror, offs_t addrselect, read64m_delegate rhandler, write64m_delegate whandler, u64 unitmask = 0, int cswidth = 0) override
	{ install_readwrite_handler_impl(addrstart, addrend, addrmask, addrmirror, addrselect, unitmask, cswidth, rhandler, whandler); }

	void install_read_handler(offs_t addrstart, offs_t addrend, offs_t addrmask, offs_t addrmirror, offs_t addrselect, read8s_delegate rhandler, u64 unitmask = 0, int cswidth = 0) override
	{ install_read_handler_impl(addrstart, addrend, addrmask, addrmirror, addrselect, unitmask, cswidth, rhandler); }
	void install_write_handler(offs_t addrstart, offs_t addrend, offs_t addrmask, offs_t addrmirror, offs_t addrselect, write8s_delegate whandler, u64 unitmask = 0, int cswidth = 0) override
	{ install_write_handler_impl(addrstart, addrend, addrmask, addrmirror, addrselect, unitmask, cswidth, whandler); }
	void install_readwrite_handler(offs_t addrstart, offs_t addrend, offs_t addrmask, offs_t addrmirror, offs_t addrselect, read8s_delegate rhandler, write8s_delegate whandler, u64 unitmask = 0, int cswidth = 0) override
	{ install_readwrite_handler_impl(addrstart, addrend, addrmask, addrmirror, addrselect, unitmask, cswidth, rhandler, whandler); }
	void install_read_handler(offs_t addrstart, offs_t addrend, offs_t addrmask, offs_t addrmirror, offs_t addrselect, read16s_delegate rhandler, u64 unitmask = 0, int cswidth = 0) override
	{ install_read_handler_impl(addrstart, addrend, addrmask, addrmirror, addrselect, unitmask, cswidth, rhandler); }
	void install_write_handler(offs_t addrstart, offs_t addrend, offs_t addrmask, offs_t addrmirror, offs_t addrselect, write16s_delegate whandler, u64 unitmask = 0, int cswidth = 0) override
	{ install_write_handler_impl(addrstart, addrend, addrmask, addrmirror, addrselect, unitmask, cswidth, whandler); }
	void install_readwrite_handler(offs_t addrstart, offs_t addrend, offs_t addrmask, offs_t addrmirror, offs_t addrselect, read16s_delegate rhandler, write16s_delegate whandler, u64 unitmask = 0, int cswidth = 0) override
	{ install_readwrite_handler_impl(addrstart, addrend, addrmask, addrmirror, addrselect, unitmask, cswidth, rhandler, whandler); }
	void install_read_handler(offs_t addrstart, offs_t addrend, offs_t addrmask, offs_t addrmirror, offs_t addrselect, read32s_delegate rhandler, u64 unitmask = 0, int cswidth = 0) override
	{ install_read_handler_impl(addrstart, addrend, addrmask, addrmirror, addrselect, unitmask, cswidth, rhandler); }
	void install_write_handler(offs_t addrstart, offs_t addrend, offs_t addrmask, offs_t addrmirror, offs_t addrselect, write32s_delegate whandler, u64 unitmask = 0, int cswidth = 0) override
	{ install_write_handler_impl(addrstart, addrend, addrmask, addrmirror, addrselect, unitmask, cswidth, whandler); }
	void install_readwrite_handler(offs_t addrstart, offs_t addrend, offs_t addrmask, offs_t addrmirror, offs_t addrselect, read32s_delegate rhandler, write32s_delegate whandler, u64 unitmask = 0, int cswidth = 0) override
	{ install_readwrite_handler_impl(addrstart, addrend, addrmask, addrmirror, addrselect, unitmask, cswidth, rhandler, whandler); }
	void install_read_handler(offs_t addrstart, offs_t addrend, offs_t addrmask, offs_t addrmirror, offs_t addrselect, read64s_delegate rhandler, u64 unitmask = 0, int cswidth = 0) override
	{ install_read_handler_impl(addrstart, addrend, addrmask, addrmirror, addrselect, unitmask, cswidth, rhandler); }
	void install_write_handler(offs_t addrstart, offs_t addrend, offs_t addrmask, offs_t addrmirror, offs_t addrselect, write64s_delegate whandler, u64 unitmask = 0, int cswidth = 0) override
	{ install_write_handler_impl(addrstart, addrend, addrmask, addrmirror, addrselect, unitmask, cswidth, whandler); }
	void install_readwrite_handler(offs_t addrstart, offs_t addrend, offs_t addrmask, offs_t addrmirror, offs_t addrselect, read64s_delegate rhandler, write64s_delegate whandler, u64 unitmask = 0, int cswidth = 0) override
	{ install_readwrite_handler_impl(addrstart, addrend, addrmask, addrmirror, addrselect, unitmask, cswidth, rhandler, whandler); }

	void install_read_handler(offs_t addrstart, offs_t addrend, offs_t addrmask, offs_t addrmirror, offs_t addrselect, read8sm_delegate rhandler, u64 unitmask = 0, int cswidth = 0) override
	{ install_read_handler_impl(addrstart, addrend, addrmask, addrmirror, addrselect, unitmask, cswidth, rhandler); }
	void install_write_handler(offs_t addrstart, offs_t addrend, offs_t addrmask, offs_t addrmirror, offs_t addrselect, write8sm_delegate whandler, u64 unitmask = 0, int cswidth = 0) override
	{ install_write_handler_impl(addrstart, addrend, addrmask, addrmirror, addrselect, unitmask, cswidth, whandler); }
	void install_readwrite_handler(offs_t addrstart, offs_t addrend, offs_t addrmask, offs_t addrmirror, offs_t addrselect, read8sm_delegate rhandler, write8sm_delegate whandler, u64 unitmask = 0, int cswidth = 0) override
	{ install_readwrite_handler_impl(addrstart, addrend, addrmask, addrmirror, addrselect, unitmask, cswidth, rhandler, whandler); }
	void install_read_handler(offs_t addrstart, offs_t addrend, offs_t addrmask, offs_t addrmirror, offs_t addrselect, read16sm_delegate rhandler, u64 unitmask = 0, int cswidth = 0) override
	{ install_read_handler_impl(addrstart, addrend, addrmask, addrmirror, addrselect, unitmask, cswidth, rhandler); }
	void install_write_handler(offs_t addrstart, offs_t addrend, offs_t addrmask, offs_t addrmirror, offs_t addrselect, write16sm_delegate whandler, u64 unitmask = 0, int cswidth = 0) override
	{ install_write_handler_impl(addrstart, addrend, addrmask, addrmirror, addrselect, unitmask, cswidth, whandler); }
	void install_readwrite_handler(offs_t addrstart, offs_t addrend, offs_t addrmask, offs_t addrmirror, offs_t addrselect, read16sm_delegate rhandler, write16sm_delegate whandler, u64 unitmask = 0, int cswidth = 0) override
	{ install_readwrite_handler_impl(addrstart, addrend, addrmask, addrmirror, addrselect, unitmask, cswidth, rhandler, whandler); }
	void install_read_handler(offs_t addrstart, offs_t addrend, offs_t addrmask, offs_t addrmirror, offs_t addrselect, read32sm_delegate rhandler, u64 unitmask = 0, int cswidth = 0) override
	{ install_read_handler_impl(addrstart, addrend, addrmask, addrmirror, addrselect, unitmask, cswidth, rhandler); }
	void install_write_handler(offs_t addrstart, offs_t addrend, offs_t addrmask, offs_t addrmirror, offs_t addrselect, write32sm_delegate whandler, u64 unitmask = 0, int cswidth = 0) override
	{ install_write_handler_impl(addrstart, addrend, addrmask, addrmirror, addrselect, unitmask, cswidth, whandler); }
	void install_readwrite_handler(offs_t addrstart, offs_t addrend, offs_t addrmask, offs_t addrmirror, offs_t addrselect, read32sm_delegate rhandler, write32sm_delegate whandler, u64 unitmask = 0, int cswidth = 0) override
	{ install_readwrite_handler_impl(addrstart, addrend, addrmask, addrmirror, addrselect, unitmask, cswidth, rhandler, whandler); }
	void install_read_handler(offs_t addrstart, offs_t addrend, offs_t addrmask, offs_t addrmirror, offs_t addrselect, read64sm_delegate rhandler, u64 unitmask = 0, int cswidth = 0) override
	{ install_read_handler_impl(addrstart, addrend, addrmask, addrmirror, addrselect, unitmask, cswidth, rhandler); }
	void install_write_handler(offs_t addrstart, offs_t addrend, offs_t addrmask, offs_t addrmirror, offs_t addrselect, write64sm_delegate whandler, u64 unitmask = 0, int cswidth = 0) override
	{ install_write_handler_impl(addrstart, addrend, addrmask, addrmirror, addrselect, unitmask, cswidth, whandler); }
	void install_readwrite_handler(offs_t addrstart, offs_t addrend, offs_t addrmask, offs_t addrmirror, offs_t addrselect, read64sm_delegate rhandler, write64sm_delegate whandler, u64 unitmask = 0, int cswidth = 0) override
	{ install_readwrite_handler_impl(addrstart, addrend, addrmask, addrmirror, addrselect, unitmask, cswidth, rhandler, whandler); }

	void install_read_handler(offs_t addrstart, offs_t addrend, offs_t addrmask, offs_t addrmirror, offs_t addrselect, read8mo_delegate rhandler, u64 unitmask = 0, int cswidth = 0) override
	{ install_read_handler_impl(addrstart, addrend, addrmask, addrmirror, addrselect, unitmask, cswidth, rhandler); }
	void install_write_handler(offs_t addrstart, offs_t addrend, offs_t addrmask, offs_t addrmirror, offs_t addrselect, write8mo_delegate whandler, u64 unitmask = 0, int cswidth = 0) override
	{ install_write_handler_impl(addrstart, addrend, addrmask, addrmirror, addrselect, unitmask, cswidth, whandler); }
	void install_readwrite_handler(offs_t addrstart, offs_t addrend, offs_t addrmask, offs_t addrmirror, offs_t addrselect, read8mo_delegate rhandler, write8mo_delegate whandler, u64 unitmask = 0, int cswidth = 0) override
	{ install_readwrite_handler_impl(addrstart, addrend, addrmask, addrmirror, addrselect, unitmask, cswidth, rhandler, whandler); }
	void install_read_handler(offs_t addrstart, offs_t addrend, offs_t addrmask, offs_t addrmirror, offs_t addrselect, read16mo_delegate rhandler, u64 unitmask = 0, int cswidth = 0) override
	{ install_read_handler_impl(addrstart, addrend, addrmask, addrmirror, addrselect, unitmask, cswidth, rhandler); }
	void install_write_handler(offs_t addrstart, offs_t addrend, offs_t addrmask, offs_t addrmirror, offs_t addrselect, write16mo_delegate whandler, u64 unitmask = 0, int cswidth = 0) override
	{ install_write_handler_impl(addrstart, addrend, addrmask, addrmirror, addrselect, unitmask, cswidth, whandler); }
	void install_readwrite_handler(offs_t addrstart, offs_t addrend, offs_t addrmask, offs_t addrmirror, offs_t addrselect, read16mo_delegate rhandler, write16mo_delegate whandler, u64 unitmask = 0, int cswidth = 0) override
	{ install_readwrite_handler_impl(addrstart, addrend, addrmask, addrmirror, addrselect, unitmask, cswidth, rhandler, whandler); }
	void install_read_handler(offs_t addrstart, offs_t addrend, offs_t addrmask, offs_t addrmirror, offs_t addrselect, read32mo_delegate rhandler, u64 unitmask = 0, int cswidth = 0) override
	{ install_read_handler_impl(addrstart, addrend, addrmask, addrmirror, addrselect, unitmask, cswidth, rhandler); }
	void install_write_handler(offs_t addrstart, offs_t addrend, offs_t addrmask, offs_t addrmirror, offs_t addrselect, write32mo_delegate whandler, u64 unitmask = 0, int cswidth = 0) override
	{ install_write_handler_impl(addrstart, addrend, addrmask, addrmirror, addrselect, unitmask, cswidth, whandler); }
	void install_readwrite_handler(offs_t addrstart, offs_t addrend, offs_t addrmask, offs_t addrmirror, offs_t addrselect, read32mo_delegate rhandler, write32mo_delegate whandler, u64 unitmask = 0, int cswidth = 0) override
	{ install_readwrite_handler_impl(addrstart, addrend, addrmask, addrmirror, addrselect, unitmask, cswidth, rhandler, whandler); }
	void install_read_handler(offs_t addrstart, offs_t addrend, offs_t addrmask, offs_t addrmirror, offs_t addrselect, read64mo_delegate rhandler, u64 unitmask = 0, int cswidth = 0) override
	{ install_read_handler_impl(addrstart, addrend, addrmask, addrmirror, addrselect, unitmask, cswidth, rhandler); }
	void install_write_handler(offs_t addrstart, offs_t addrend, offs_t addrmask, offs_t addrmirror, offs_t addrselect, write64mo_delegate whandler, u64 unitmask = 0, int cswidth = 0) override
	{ install_write_handler_impl(addrstart, addrend, addrmask, addrmirror, addrselect, unitmask, cswidth, whandler); }
	void install_readwrite_handler(offs_t addrstart, offs_t addrend, offs_t addrmask, offs_t addrmirror, offs_t addrselect, read64mo_delegate rhandler, write64mo_delegate whandler, u64 unitmask = 0, int cswidth = 0) override
	{ install_readwrite_handler_impl(addrstart, addrend, addrmask, addrmirror, addrselect, unitmask, cswidth, rhandler, whandler); }

	void install_read_handler(offs_t addrstart, offs_t addrend, offs_t addrmask, offs_t addrmirror, offs_t addrselect, read8smo_delegate rhandler, u64 unitmask = 0, int cswidth = 0) override
	{ install_read_handler_impl(addrstart, addrend, addrmask, addrmirror, addrselect, unitmask, cswidth, rhandler); }
	void install_write_handler(offs_t addrstart, offs_t addrend, offs_t addrmask, offs_t addrmirror, offs_t addrselect, write8smo_delegate whandler, u64 unitmask = 0, int cswidth = 0) override
	{ install_write_handler_impl(addrstart, addrend, addrmask, addrmirror, addrselect, unitmask, cswidth, whandler); }
	void install_readwrite_handler(offs_t addrstart, offs_t addrend, offs_t addrmask, offs_t addrmirror, offs_t addrselect, read8smo_delegate rhandler, write8smo_delegate whandler, u64 unitmask = 0, int cswidth = 0) override
	{ install_readwrite_handler_impl(addrstart, addrend, addrmask, addrmirror, addrselect, unitmask, cswidth, rhandler, whandler); }
	void install_read_handler(offs_t addrstart, offs_t addrend, offs_t addrmask, offs_t addrmirror, offs_t addrselect, read16smo_delegate rhandler, u64 unitmask = 0, int cswidth = 0) override
	{ install_read_handler_impl(addrstart, addrend, addrmask, addrmirror, addrselect, unitmask, cswidth, rhandler); }
	void install_write_handler(offs_t addrstart, offs_t addrend, offs_t addrmask, offs_t addrmirror, offs_t addrselect, write16smo_delegate whandler, u64 unitmask = 0, int cswidth = 0) override
	{ install_write_handler_impl(addrstart, addrend, addrmask, addrmirror, addrselect, unitmask, cswidth, whandler); }
	void install_readwrite_handler(offs_t addrstart, offs_t addrend, offs_t addrmask, offs_t addrmirror, offs_t addrselect, read16smo_delegate rhandler, write16smo_delegate whandler, u64 unitmask = 0, int cswidth = 0) override
	{ install_readwrite_handler_impl(addrstart, addrend, addrmask, addrmirror, addrselect, unitmask, cswidth, rhandler, whandler); }
	void install_read_handler(offs_t addrstart, offs_t addrend, offs_t addrmask, offs_t addrmirror, offs_t addrselect, read32smo_delegate rhandler, u64 unitmask = 0, int cswidth = 0) override
	{ install_read_handler_impl(addrstart, addrend, addrmask, addrmirror, addrselect, unitmask, cswidth, rhandler); }
	void install_write_handler(offs_t addrstart, offs_t addrend, offs_t addrmask, offs_t addrmirror, offs_t addrselect, write32smo_delegate whandler, u64 unitmask = 0, int cswidth = 0) override
	{ install_write_handler_impl(addrstart, addrend, addrmask, addrmirror, addrselect, unitmask, cswidth, whandler); }
	void install_readwrite_handler(offs_t addrstart, offs_t addrend, offs_t addrmask, offs_t addrmirror, offs_t addrselect, read32smo_delegate rhandler, write32smo_delegate whandler, u64 unitmask = 0, int cswidth = 0) override
	{ install_readwrite_handler_impl(addrstart, addrend, addrmask, addrmirror, addrselect, unitmask, cswidth, rhandler, whandler); }
	void install_read_handler(offs_t addrstart, offs_t addrend, offs_t addrmask, offs_t addrmirror, offs_t addrselect, read64smo_delegate rhandler, u64 unitmask = 0, int cswidth = 0) override
	{ install_read_handler_impl(addrstart, addrend, addrmask, addrmirror, addrselect, unitmask, cswidth, rhandler); }
	void install_write_handler(offs_t addrstart, offs_t addrend, offs_t addrmask, offs_t addrmirror, offs_t addrselect, write64smo_delegate whandler, u64 unitmask = 0, int cswidth = 0) override
	{ install_write_handler_impl(addrstart, addrend, addrmask, addrmirror, addrselect, unitmask, cswidth, whandler); }
	void install_readwrite_handler(offs_t addrstart, offs_t addrend, offs_t addrmask, offs_t addrmirror, offs_t addrselect, read64smo_delegate rhandler, write64smo_delegate whandler, u64 unitmask = 0, int cswidth = 0) override
	{ install_readwrite_handler_impl(addrstart, addrend, addrmask, addrmirror, addrselect, unitmask, cswidth, rhandler, whandler); }

	template<typename READ>
	void install_read_handler_impl(offs_t addrstart, offs_t addrend, offs_t addrmask, offs_t addrmirror, offs_t addrselect, u64 unitmask, int cswidth, READ &handler_r)
	{
		try { handler_r.resolve(); }
		catch (const binding_type_exception &) {
			osd_printf_error("Binding error while installing read handler %s for range 0x%X-0x%X mask 0x%X mirror 0x%X select 0x%X umask 0x%X\n", handler_r.name(), addrstart, addrend, addrmask, addrmirror, addrselect, unitmask);
			throw;
		}
		install_read_handler_helper<handler_width<READ>::value>(addrstart, addrend, addrmask, addrmirror, addrselect, unitmask, cswidth, handler_r);
	}

	template<typename WRITE>
	void install_write_handler_impl(offs_t addrstart, offs_t addrend, offs_t addrmask, offs_t addrmirror, offs_t addrselect, u64 unitmask, int cswidth, WRITE &handler_w)
	{
		try { handler_w.resolve(); }
		catch (const binding_type_exception &) {
			osd_printf_error("Binding error while installing write handler %s for range 0x%X-0x%X mask 0x%X mirror 0x%X select 0x%X umask 0x%X\n", handler_w.name(), addrstart, addrend, addrmask, addrmirror, addrselect, unitmask);
			throw;
		}
		install_write_handler_helper<handler_width<WRITE>::value>(addrstart, addrend, addrmask, addrmirror, addrselect, unitmask, cswidth, handler_w);
	}

	template<typename READ, typename WRITE>
	void install_readwrite_handler_impl(offs_t addrstart, offs_t addrend, offs_t addrmask, offs_t addrmirror, offs_t addrselect, u64 unitmask, int cswidth, READ &handler_r, WRITE &handler_w)
	{
		static_assert(handler_width<READ>::value == handler_width<WRITE>::value, "handler widths do not match");
		try { handler_r.resolve(); }
		catch (const binding_type_exception &) {
			osd_printf_error("Binding error while installing read handler %s for range 0x%X-0x%X mask 0x%X mirror 0x%X select 0x%X umask 0x%X\n", handler_r.name(), addrstart, addrend, addrmask, addrmirror, addrselect, unitmask);
			throw;
		}
		try { handler_w.resolve(); }
		catch (const binding_type_exception &) {
			osd_printf_error("Binding error while installing write handler %s for range 0x%X-0x%X mask 0x%X mirror 0x%X select 0x%X umask 0x%X\n", handler_w.name(), addrstart, addrend, addrmask, addrmirror, addrselect, unitmask);
			throw;
		}
		install_readwrite_handler_helper<handler_width<READ>::value>(addrstart, addrend, addrmask, addrmirror, addrselect, unitmask, cswidth, handler_r, handler_w);
	}

	template<int AccessWidth, typename READ> std::enable_if_t<(Width == AccessWidth)>
	install_read_handler_helper(offs_t addrstart, offs_t addrend, offs_t addrmask, offs_t addrmirror, offs_t addrselect, u64 unitmask, int cswidth, const READ &handler_r)
	{
		VPRINTF("memory_view::install_read_handler(%*x-%*x mask=%*x mirror=%*x, space width=%d, handler width=%d, %s, %*x)\n",
				m_addrchars, addrstart, m_addrchars, addrend,
				m_addrchars, addrmask, m_addrchars, addrmirror,
				8 << Width, 8 << AccessWidth,
				handler_r.name(), data_width() / 4, unitmask);

		offs_t nstart, nend, nmask, nmirror;
		u64 nunitmask;
		int ncswidth;
		check_range_optimize_all("install_read_handler", 8 << AccessWidth, addrstart, addrend, addrmask, addrmirror, addrselect, unitmask, cswidth, nstart, nend, nmask, nmirror, nunitmask, ncswidth);

		m_view.m_select_u(m_id);
		auto hand_r = new handler_entry_read_delegate<Width, AddrShift, Endian, READ>(m_view.m_space, handler_r);
		hand_r->set_address_info(nstart, nmask);
		r()->populate(nstart, nend, nmirror, hand_r);
		invalidate_caches(read_or_write::READ);
	}

	template<int AccessWidth, typename READ> std::enable_if_t<(Width > AccessWidth)>
	install_read_handler_helper(offs_t addrstart, offs_t addrend, offs_t addrmask, offs_t addrmirror, offs_t addrselect, u64 unitmask, int cswidth,
								const READ &handler_r)
	{
		VPRINTF("memory_view::install_read_handler(%*x-%*x mask=%*x mirror=%*x, space width=%d, handler width=%d, %s, %*x)\n",
				m_addrchars, addrstart, m_addrchars, addrend,
				m_addrchars, addrmask, m_addrchars, addrmirror,
				8 << Width, 8 << AccessWidth,
				handler_r.name(), data_width() / 4, unitmask);

		offs_t nstart, nend, nmask, nmirror;
		u64 nunitmask;
		int ncswidth;
		check_range_optimize_all("install_read_handler", 8 << AccessWidth, addrstart, addrend, addrmask, addrmirror, addrselect, unitmask, cswidth, nstart, nend, nmask, nmirror, nunitmask, ncswidth);

		m_view.m_select_u(m_id);
		auto hand_r = new handler_entry_read_delegate<AccessWidth, -AccessWidth, Endian, READ>(m_view.m_space, handler_r);
		memory_units_descriptor<Width, AddrShift, Endian> descriptor(AccessWidth, Endian, hand_r, nstart, nend, nmask, nunitmask, ncswidth);
		hand_r->set_address_info(descriptor.get_handler_start(), descriptor.get_handler_mask());
		r()->populate_mismatched(nstart, nend, nmirror, descriptor);
		hand_r->unref();
		invalidate_caches(read_or_write::READ);
	}


	template<int AccessWidth, typename READ> std::enable_if_t<(Width < AccessWidth)>
	install_read_handler_helper(offs_t addrstart, offs_t addrend, offs_t addrmask, offs_t addrmirror, offs_t addrselect, u64 unitmask, int cswidth,
								const READ &handler_r)
	{
		fatalerror("install_read_handler: cannot install a %d-wide handler in a %d-wide bus", 8 << AccessWidth, 8 << Width);
	}

	template<int AccessWidth, typename WRITE> std::enable_if_t<(Width == AccessWidth)>
	install_write_handler_helper(offs_t addrstart, offs_t addrend, offs_t addrmask, offs_t addrmirror, offs_t addrselect, u64 unitmask, int cswidth,
								 const WRITE &handler_w)
	{
		VPRINTF("memory_view::install_write_handler(%*x-%*x mask=%*x mirror=%*x, space width=%d, handler width=%d, %s, %*x)\n",
				m_addrchars, addrstart, m_addrchars, addrend,
				m_addrchars, addrmask, m_addrchars, addrmirror,
				8 << Width, 8 << AccessWidth,
				handler_w.name(), data_width() / 4, unitmask);

		offs_t nstart, nend, nmask, nmirror;
		u64 nunitmask;
		int ncswidth;
		check_range_optimize_all("install_write_handler", 8 << AccessWidth, addrstart, addrend, addrmask, addrmirror, addrselect, unitmask, cswidth, nstart, nend, nmask, nmirror, nunitmask, ncswidth);

		m_view.m_select_u(m_id);
		auto hand_w = new handler_entry_write_delegate<Width, AddrShift, Endian, WRITE>(m_view.m_space, handler_w);
		hand_w->set_address_info(nstart, nmask);
		w()->populate(nstart, nend, nmirror, hand_w);
		invalidate_caches(read_or_write::WRITE);
	}

	template<int AccessWidth, typename WRITE> std::enable_if_t<(Width > AccessWidth)>
	install_write_handler_helper(offs_t addrstart, offs_t addrend, offs_t addrmask, offs_t addrmirror, offs_t addrselect, u64 unitmask, int cswidth,
								 const WRITE &handler_w)
	{
		VPRINTF("memory_view::install_write_handler(%*x-%*x mask=%*x mirror=%*x, space width=%d, handler width=%d, %s, %*x)\n",
				m_addrchars, addrstart, m_addrchars, addrend,
				m_addrchars, addrmask, m_addrchars, addrmirror,
				8 << Width, 8 << AccessWidth,
				handler_w.name(), data_width() / 4, unitmask);

		offs_t nstart, nend, nmask, nmirror;
		u64 nunitmask;
		int ncswidth;
		check_range_optimize_all("install_write_handler", 8 << AccessWidth, addrstart, addrend, addrmask, addrmirror, addrselect, unitmask, cswidth, nstart, nend, nmask, nmirror, nunitmask, ncswidth);

		m_view.m_select_u(m_id);
		auto hand_w = new handler_entry_write_delegate<AccessWidth, -AccessWidth, Endian, WRITE>(m_view.m_space, handler_w);
		memory_units_descriptor<Width, AddrShift, Endian> descriptor(AccessWidth, Endian, hand_w, nstart, nend, nmask, nunitmask, ncswidth);
		hand_w->set_address_info(descriptor.get_handler_start(), descriptor.get_handler_mask());
		w()->populate_mismatched(nstart, nend, nmirror, descriptor);
		hand_w->unref();
		invalidate_caches(read_or_write::WRITE);
	}


	template<int AccessWidth, typename WRITE> std::enable_if_t<(Width < AccessWidth)>
	install_write_handler_helper(offs_t addrstart, offs_t addrend, offs_t addrmask, offs_t addrmirror, offs_t addrselect, u64 unitmask, int cswidth,
								 const WRITE &handler_w)
	{
		fatalerror("install_write_handler: cannot install a %d-wide handler in a %d-wide bus", 8 << AccessWidth, 8 << Width);
	}



	template<int AccessWidth, typename READ, typename WRITE> std::enable_if_t<(Width == AccessWidth)>
	install_readwrite_handler_helper(offs_t addrstart, offs_t addrend, offs_t addrmask, offs_t addrmirror, offs_t addrselect, u64 unitmask, int cswidth,
									 const READ  &handler_r,
									 const WRITE &handler_w)
	{
		VPRINTF("memory_view::install_readwrite_handler(%*x-%*x mask=%*x mirror=%*x, space width=%d, handler width=%d, %s, %s, %*x)\n",
				m_addrchars, addrstart, m_addrchars, addrend,
				m_addrchars, addrmask, m_addrchars, addrmirror,
				8 << Width, 8 << AccessWidth,
				handler_r.name(), handler_w.name(), data_width() / 4, unitmask);

		offs_t nstart, nend, nmask, nmirror;
		u64 nunitmask;
		int ncswidth;
		check_range_optimize_all("install_readwrite_handler", 8 << AccessWidth, addrstart, addrend, addrmask, addrmirror, addrselect, unitmask, cswidth, nstart, nend, nmask, nmirror, nunitmask, ncswidth);

		m_view.m_select_u(m_id);
		auto hand_r = new handler_entry_read_delegate <Width, AddrShift, Endian, READ>(m_view.m_space, handler_r);
		hand_r->set_address_info(nstart, nmask);
		r() ->populate(nstart, nend, nmirror, hand_r);

		auto hand_w = new handler_entry_write_delegate<Width, AddrShift, Endian, WRITE>(m_view.m_space, handler_w);
		hand_w->set_address_info(nstart, nmask);
		w()->populate(nstart, nend, nmirror, hand_w);

		invalidate_caches(read_or_write::READWRITE);
	}

	template<int AccessWidth, typename READ, typename WRITE> std::enable_if_t<(Width > AccessWidth)>
	install_readwrite_handler_helper(offs_t addrstart, offs_t addrend, offs_t addrmask, offs_t addrmirror, offs_t addrselect, u64 unitmask, int cswidth,
									 const READ  &handler_r,
									 const WRITE &handler_w)
	{
		VPRINTF("memory_view::install_readwrite_handler(%*x-%*x mask=%*x mirror=%*x, space width=%d, handler width=%d, %s, %s, %*x)\n",
				m_addrchars, addrstart, m_addrchars, addrend,
				m_addrchars, addrmask, m_addrchars, addrmirror,
				8 << Width, 8 << AccessWidth,
				handler_r.name(), handler_w.name(), data_width() / 4, unitmask);

		offs_t nstart, nend, nmask, nmirror;
		u64 nunitmask;
		int ncswidth;
		check_range_optimize_all("install_readwrite_handler", 8 << AccessWidth, addrstart, addrend, addrmask, addrmirror, addrselect, unitmask, cswidth, nstart, nend, nmask, nmirror, nunitmask, ncswidth);

		m_view.m_select_u(m_id);
		auto hand_r = new handler_entry_read_delegate <AccessWidth, -AccessWidth, Endian, READ>(m_view.m_space, handler_r);
		memory_units_descriptor<Width, AddrShift, Endian> descriptor(AccessWidth, Endian, hand_r, nstart, nend, nmask, nunitmask, ncswidth);
		hand_r->set_address_info(descriptor.get_handler_start(), descriptor.get_handler_mask());
		r() ->populate_mismatched(nstart, nend, nmirror, descriptor);
		hand_r->unref();

		auto hand_w = new handler_entry_write_delegate<AccessWidth, -AccessWidth, Endian, WRITE>(m_view.m_space, handler_w);
		descriptor.set_subunit_handler(hand_w);
		hand_w->set_address_info(descriptor.get_handler_start(), descriptor.get_handler_mask());
		w()->populate_mismatched(nstart, nend, nmirror, descriptor);
		hand_w->unref();

		invalidate_caches(read_or_write::READWRITE);
	}


	template<int AccessWidth, typename READ, typename WRITE> std::enable_if_t<(Width < AccessWidth)>
	install_readwrite_handler_helper(offs_t addrstart, offs_t addrend, offs_t addrmask, offs_t addrmirror, offs_t addrselect, u64 unitmask, int cswidth,
									 const READ  &handler_r,
									 const WRITE &handler_w)
	{
		fatalerror("install_readwrite_handler: cannot install a %d-wide handler in a %d-wide bus", 8 << AccessWidth, 8 << Width);
	}
};

namespace {
	template<int Level, int Width, int AddrShift, endianness_t Endian> memory_view::memory_view_entry *mve_make_1(const address_space_config &config, memory_manager &manager, memory_view &view, int id) {
		return new memory_view_entry_specific<Level, Width, AddrShift, Endian>(config, manager, view, id);
	}

	template<int Width, int AddrShift, endianness_t Endian> memory_view::memory_view_entry *mve_make_2(int Level, const address_space_config &config, memory_manager &manager, memory_view &view, int id) {
		switch(Level) {
		case 0: return mve_make_1<0, Width, AddrShift, Endian>(config, manager, view, id);
		case 1: return mve_make_1<1, Width, AddrShift, Endian>(config, manager, view, id);
		default: abort();
		}
	}

	template<int Width, int AddrShift> memory_view::memory_view_entry *mve_make_3(int Level, endianness_t Endian, const address_space_config &config, memory_manager &manager, memory_view &view, int id) {
		switch(Endian) {
		case ENDIANNESS_LITTLE: return mve_make_2<Width, AddrShift, ENDIANNESS_LITTLE>(Level, config, manager, view, id);
		case ENDIANNESS_BIG:    return mve_make_2<Width, AddrShift, ENDIANNESS_BIG>   (Level, config, manager, view, id);
		default: abort();
		}
	}

	memory_view::memory_view_entry *mve_make(int Level, int Width, int AddrShift, endianness_t Endian, const address_space_config &config, memory_manager &manager, memory_view &view, int id) {
		switch (Width | (AddrShift + 4)) {
		case  8|(4+1): return mve_make_3<0,  1>(Level, Endian, config, manager, view, id); 
		case  8|(4-0): return mve_make_3<0,  0>(Level, Endian, config, manager, view, id);
		case 16|(4+3): return mve_make_3<1,  3>(Level, Endian, config, manager, view, id);
		case 16|(4-0): return mve_make_3<1,  0>(Level, Endian, config, manager, view, id);
		case 16|(4-1): return mve_make_3<1, -1>(Level, Endian, config, manager, view, id);
		case 32|(4+3): return mve_make_3<2,  3>(Level, Endian, config, manager, view, id);
		case 32|(4-0): return mve_make_3<2,  0>(Level, Endian, config, manager, view, id);
		case 32|(4-1): return mve_make_3<2, -1>(Level, Endian, config, manager, view, id);
		case 32|(4-2): return mve_make_3<2, -2>(Level, Endian, config, manager, view, id);
		case 64|(4-0): return mve_make_3<3,  0>(Level, Endian, config, manager, view, id);
		case 64|(4-1): return mve_make_3<3, -1>(Level, Endian, config, manager, view, id);
		case 64|(4-2): return mve_make_3<3, -2>(Level, Endian, config, manager, view, id);
		case 64|(4-3): return mve_make_3<3, -3>(Level, Endian, config, manager, view, id);
		default: abort();
		}
	}
};

memory_view::memory_view_entry &memory_view::operator[](int slot)
{
	if(!m_config)
		fatalerror("A view must be in a map or a space before it can be setup.");

	auto i = m_entry_mapping.find(slot);
	if(i == m_entry_mapping.end()) {
		memory_view_entry *e;
		int id = m_entries.size();
		e = mve_make(emu::detail::handler_entry_dispatch_level(m_config->addr_width()), m_config->data_width(), m_config->addr_shift(), m_config->endianness(),
					 *m_config, m_device.machine().memory(), *this, id);
		m_entries.resize(id+1);
		m_entries[id].reset(e);
		m_entry_mapping[slot] = id;
		return *e;

	} else
		return *m_entries[i->second];
}

memory_view::memory_view_entry::memory_view_entry(const address_space_config &config, memory_manager &manager, memory_view &view, int id) : address_space_installer(config, manager), m_view(view), m_id(id)
{
	m_map = std::make_unique<address_map>(m_view);
}

template<int Level, int Width, int AddrShift, endianness_t Endian> void memory_view_entry_specific<Level, Width, AddrShift, Endian>::populate_from_map(address_map *map)
{
	// no map specified, use the space-specific one
	if (map == nullptr)
		map = m_map.get();

	memory_region *devregion = (m_view.m_space->spacenum() == 0) ? m_view.m_device.memregion(DEVICE_SELF) : nullptr;
	u32 devregionsize = (devregion != nullptr) ? devregion->bytes() : 0;

	// merge in the submaps
	map->import_submaps(m_manager.machine(), m_view.m_device.owner() ? *m_view.m_device.owner() : m_view.m_device, data_width(), endianness(), addr_shift());

	// make a pass over the address map, adjusting for the device and getting memory pointers
	for (address_map_entry &entry : map->m_entrylist)
	{
		// computed adjusted addresses first
		adjust_addresses(entry.m_addrstart, entry.m_addrend, entry.m_addrmask, entry.m_addrmirror);

		// if we have a share entry, add it to our map
		if (entry.m_share != nullptr)
		{
			// if we can't find it, add it to our map
			std::string fulltag = entry.m_devbase.subtag(entry.m_share);
			memory_share *share = m_manager.share_find(fulltag);
			if (!share)
			{
				VPRINTF("Creating share '%s' of length 0x%X\n", fulltag.c_str(), entry.m_addrend + 1 - entry.m_addrstart);
				share = m_manager.share_alloc(m_view.m_device, fulltag, data_width(), address_to_byte(entry.m_addrend + 1 - entry.m_addrstart), endianness());
			}
			else
			{
				std::string result = share->compare(data_width(), address_to_byte(entry.m_addrend + 1 - entry.m_addrstart), endianness());
				if (!result.empty())
					fatalerror("%s\n", result);
			}
			entry.m_memory = share->ptr();
		}

		// if this is a ROM handler without a specified region and not shared, attach it to the implicit region
		if (m_view.m_space->spacenum() == AS_PROGRAM && entry.m_read.m_type == AMH_ROM && entry.m_region == nullptr && entry.m_share == nullptr)
		{
			// make sure it fits within the memory region before doing so, however
			if (entry.m_addrend < devregionsize)
			{
				entry.m_region = m_view.m_device.tag();
				entry.m_rgnoffs = address_to_byte(entry.m_addrstart);
			}
		}

		// validate adjusted addresses against implicit regions
		if (entry.m_region != nullptr)
		{
			// determine full tag
			std::string fulltag = entry.m_devbase.subtag(entry.m_region);

			// find the region
			memory_region *region = m_manager.machine().root_device().memregion(fulltag);
			if (region == nullptr)
				fatalerror("device '%s' %s view memory map entry %X-%X references nonexistent region \"%s\"\n", m_view.m_device.tag(), m_view.m_name, entry.m_addrstart, entry.m_addrend, entry.m_region);

			// validate the region
			if (entry.m_rgnoffs + m_config.addr2byte(entry.m_addrend - entry.m_addrstart + 1) > region->bytes())
				fatalerror("device '%s' %s view memory map entry %X-%X extends beyond region \"%s\" size (%X)\n", m_view.m_device.tag(), m_view.m_name, entry.m_addrstart, entry.m_addrend, entry.m_region, region->bytes());

			if (entry.m_share != nullptr)
				fatalerror("device '%s' %s view memory map entry %X-%X has both .region() and .share()\n", m_view.m_device.tag(), m_view.m_name, entry.m_addrstart, entry.m_addrend);
		}

		// convert any region-relative entries to their memory pointers
		if (entry.m_region != nullptr)
		{
			// determine full tag
			std::string fulltag = entry.m_devbase.subtag(entry.m_region);

			// set the memory address
			entry.m_memory = m_manager.machine().root_device().memregion(fulltag)->base() + entry.m_rgnoffs;
		}

		// allocate anonymous ram when needed
		if (!entry.m_memory && (entry.m_read.m_type == AMH_RAM || entry.m_write.m_type == AMH_RAM))
			entry.m_memory = m_manager.anonymous_alloc(*m_view.m_space, address_to_byte(entry.m_addrend + 1 - entry.m_addrstart), m_config.data_width(), entry.m_addrstart, entry.m_addrend, key());
	}

	// Force the slot to exist, in case the map is empty
	m_view.m_select_u(m_id);

	// install the handlers, using the original, unadjusted memory map
	for(const address_map_entry &entry : map->m_entrylist) {
		// map both read and write halves
		populate_map_entry(entry, read_or_write::READ);
		populate_map_entry(entry, read_or_write::WRITE);
	}
}

std::string memory_view::memory_view_entry::key() const
{
	std::string key = m_view.m_context;
	if(m_id != -1)
		key += util::string_format("%s[%d].", m_view.m_name, m_view.id_to_slot(m_id));
	return key;
}


memory_view::memory_view(device_t &device, std::string name) : m_device(device), m_name(name), m_config(nullptr), m_addrstart(0), m_addrend(0), m_space(nullptr), m_handler_read(nullptr), m_handler_write(nullptr), m_cur_id(-1), m_cur_slot(-1)
{
}

void memory_view::disable()
{
	m_cur_slot = -1;
	m_cur_id = -1;
	m_select_a(-1);
}

void memory_view::select(int slot)
{
	auto i = m_entry_mapping.find(slot);
	if(i == m_entry_mapping.end())
		fatalerror("memory_view %s: select of unknown slot %d", m_name, slot);

	m_cur_slot = slot;
	m_cur_id = i->second;
	m_select_a(m_cur_id);
}

int memory_view::id_to_slot(int id) const
{
	for(const auto &p : m_entry_mapping)
		if(p.second == id)
			return p.first;
	fatalerror("memory_view::id_to_slot on unknown id %d\n", id);
}

void memory_view::initialize_from_address_map(offs_t addrstart, offs_t addrend, const address_space_config &config)
{
	if(m_config)
		fatalerror("A memory_view can be present in only one address map.");

	m_config = &config;
	m_addrstart = addrstart;
	m_addrend = addrend;
}

namespace {
	template<int HighBits, int Width, int AddrShift, endianness_t Endian> void h_make_1(address_space &space, memory_view &view, handler_entry *&r, handler_entry *&w, std::function<void (int)> &sa, std::function<void (int)> &su) {
		auto rx = new handler_entry_read_dispatch <HighBits, Width, AddrShift, Endian>(&space, view);
		auto wx = new handler_entry_write_dispatch<HighBits, Width, AddrShift, Endian>(&space, view);
		r = rx;
		w = wx;

		sa = [rx, wx](int s) { rx->select_a(s); wx->select_a(s); };
		su = [rx, wx](int s) { rx->select_u(s); wx->select_u(s); };
	}

	template<int Width, int AddrShift, endianness_t Endian> void h_make_2(int HighBits, address_space &space, memory_view &view, handler_entry *&r, handler_entry *&w, std::function<void (int)> &sa, std::function<void (int)> &su) {
		switch(HighBits) {
		case  0: h_make_1<std::max(0, Width), Width, AddrShift, Endian>(space, view, r, w, sa, su); break;
		case  1: h_make_1<std::max(1, Width), Width, AddrShift, Endian>(space, view, r, w, sa, su); break;
		case  2: h_make_1<std::max(2, Width), Width, AddrShift, Endian>(space, view, r, w, sa, su); break;
		case  3: h_make_1<std::max(3, Width), Width, AddrShift, Endian>(space, view, r, w, sa, su); break;
		case  4: h_make_1< 4, Width, AddrShift, Endian>(space, view, r, w, sa, su); break;
		case  5: h_make_1< 5, Width, AddrShift, Endian>(space, view, r, w, sa, su); break;
		case  6: h_make_1< 6, Width, AddrShift, Endian>(space, view, r, w, sa, su); break;
		case  7: h_make_1< 7, Width, AddrShift, Endian>(space, view, r, w, sa, su); break;
		case  8: h_make_1< 8, Width, AddrShift, Endian>(space, view, r, w, sa, su); break;
		case  9: h_make_1< 9, Width, AddrShift, Endian>(space, view, r, w, sa, su); break;
		case 10: h_make_1<10, Width, AddrShift, Endian>(space, view, r, w, sa, su); break;
		case 11: h_make_1<11, Width, AddrShift, Endian>(space, view, r, w, sa, su); break;
		case 12: h_make_1<12, Width, AddrShift, Endian>(space, view, r, w, sa, su); break;
		case 13: h_make_1<13, Width, AddrShift, Endian>(space, view, r, w, sa, su); break;
		case 14: h_make_1<14, Width, AddrShift, Endian>(space, view, r, w, sa, su); break;
		case 15: h_make_1<15, Width, AddrShift, Endian>(space, view, r, w, sa, su); break;
		case 16: h_make_1<16, Width, AddrShift, Endian>(space, view, r, w, sa, su); break;
		case 17: h_make_1<17, Width, AddrShift, Endian>(space, view, r, w, sa, su); break;
		case 18: h_make_1<18, Width, AddrShift, Endian>(space, view, r, w, sa, su); break;
		case 19: h_make_1<19, Width, AddrShift, Endian>(space, view, r, w, sa, su); break;
		case 20: h_make_1<20, Width, AddrShift, Endian>(space, view, r, w, sa, su); break;
		case 21: h_make_1<21, Width, AddrShift, Endian>(space, view, r, w, sa, su); break;
		case 22: h_make_1<22, Width, AddrShift, Endian>(space, view, r, w, sa, su); break;
		case 23: h_make_1<23, Width, AddrShift, Endian>(space, view, r, w, sa, su); break;
		case 24: h_make_1<24, Width, AddrShift, Endian>(space, view, r, w, sa, su); break;
		case 25: h_make_1<25, Width, AddrShift, Endian>(space, view, r, w, sa, su); break;
		case 26: h_make_1<26, Width, AddrShift, Endian>(space, view, r, w, sa, su); break;
		case 27: h_make_1<27, Width, AddrShift, Endian>(space, view, r, w, sa, su); break;
		case 28: h_make_1<28, Width, AddrShift, Endian>(space, view, r, w, sa, su); break;
		case 29: h_make_1<29, Width, AddrShift, Endian>(space, view, r, w, sa, su); break;
		case 30: h_make_1<20, Width, AddrShift, Endian>(space, view, r, w, sa, su); break;
		case 31: h_make_1<31, Width, AddrShift, Endian>(space, view, r, w, sa, su); break;
		case 32: h_make_1<32, Width, AddrShift, Endian>(space, view, r, w, sa, su); break;
		default: abort();
		}
	}

	template<int Width, int AddrShift> void h_make_3(int HighBits, endianness_t Endian, address_space &space, memory_view &view, handler_entry *&r, handler_entry *&w, std::function<void (int)> &sa, std::function<void (int)> &su) {
		switch(Endian) {
		case ENDIANNESS_LITTLE: h_make_2<Width, AddrShift, ENDIANNESS_LITTLE>(HighBits, space, view, r, w, sa, su); break;
		case ENDIANNESS_BIG:    h_make_2<Width, AddrShift, ENDIANNESS_BIG>   (HighBits, space, view, r, w, sa, su); break;
		default: abort();
		}
	}

	void h_make(int HighBits, int Width, int AddrShift, endianness_t Endian, address_space &space, memory_view &view, handler_entry *&r, handler_entry *&w, std::function<void (int)> &sa, std::function<void (int)> &su) {
		switch (Width | (AddrShift + 4)) {
		case  8|(4+1): h_make_3<0,  1>(HighBits, Endian, space, view, r, w, sa, su); break;
		case  8|(4-0): h_make_3<0,  0>(HighBits, Endian, space, view, r, w, sa, su); break;
		case 16|(4+3): h_make_3<1,  3>(HighBits, Endian, space, view, r, w, sa, su); break;
		case 16|(4-0): h_make_3<1,  0>(HighBits, Endian, space, view, r, w, sa, su); break;
		case 16|(4-1): h_make_3<1, -1>(HighBits, Endian, space, view, r, w, sa, su); break;
		case 32|(4+3): h_make_3<2,  3>(HighBits, Endian, space, view, r, w, sa, su); break;
		case 32|(4-0): h_make_3<2,  0>(HighBits, Endian, space, view, r, w, sa, su); break;
		case 32|(4-1): h_make_3<2, -1>(HighBits, Endian, space, view, r, w, sa, su); break;
		case 32|(4-2): h_make_3<2, -2>(HighBits, Endian, space, view, r, w, sa, su); break;
		case 64|(4-0): h_make_3<3,  0>(HighBits, Endian, space, view, r, w, sa, su); break;
		case 64|(4-1): h_make_3<3, -1>(HighBits, Endian, space, view, r, w, sa, su); break;
		case 64|(4-2): h_make_3<3, -2>(HighBits, Endian, space, view, r, w, sa, su); break;
		case 64|(4-3): h_make_3<3, -3>(HighBits, Endian, space, view, r, w, sa, su); break;
		default: abort();
		}
	}	
}

std::pair<handler_entry *, handler_entry *> memory_view::make_handlers(address_space &space, offs_t addrstart, offs_t addrend)
{
	if(m_space != &space || m_addrstart != addrstart || m_addrend != addrend) {
		if(m_space)
			fatalerror("A memory_view can be installed only once.");

		if(m_config) {
			if(m_addrstart != addrstart || m_addrend != addrend)
				fatalerror("A memory_view must be installed at its configuration address.");
		} else {
			m_config = &space.space_config();
			m_addrstart = addrstart;
			m_addrend = addrend;
		}

		m_space = &space;

		offs_t span = addrstart ^ addrend;
		u32 awidth = 0;
		if(span) {
			for(awidth = 1; awidth != 32; awidth++)
				if((1 << awidth) >= span)
					break;
		}

		h_make(awidth, m_config->data_width(), m_config->addr_shift(), m_config->endianness(), space, *this, m_handler_read, m_handler_write, m_select_a, m_select_u);
	}

	return std::make_pair(m_handler_read, m_handler_write);	
}

void memory_view::make_subdispatch(std::string context)
{
	m_context = context;
	for(auto &e : m_entries)
		e->populate_from_map();
}

void memory_view::memory_view_entry::check_range_optimize_mirror(const char *function, offs_t addrstart, offs_t addrend, offs_t addrmirror, offs_t &nstart, offs_t &nend, offs_t &nmask, offs_t &nmirror)
{
	check_optimize_mirror(function, addrstart, addrend, addrmirror, nstart, nend, nmask, nmirror);
	if(nstart < m_view.m_addrstart || (nend | nmirror) > m_view.m_addrend)
		fatalerror("%s: The range %x-%x mirror %x, exceeds the view window boundaries %x-%x.\n", function, addrstart, addrend, addrmirror, m_view.m_addrstart, m_view.m_addrend);
}

void memory_view::memory_view_entry::check_range_optimize_all(const char *function, int width, offs_t addrstart, offs_t addrend, offs_t addrmask, offs_t addrmirror, offs_t addrselect, u64 unitmask, int cswidth, offs_t &nstart, offs_t &nend, offs_t &nmask, offs_t &nmirror, u64 &nunitmask, int &ncswidth)
{
	check_optimize_all(function, width, addrstart, addrend, addrmask, addrmirror, addrselect, unitmask, cswidth, nstart, nend, nmask, nmirror, nunitmask, ncswidth);
	if(nstart < m_view.m_addrstart || (nend | nmirror | addrselect) > m_view.m_addrend)
		fatalerror("%s: The range %x-%x mirror %x select %x, exceeds the view window boundaries %x-%x.\n", function, addrstart, addrend, addrmirror, addrselect, m_view.m_addrstart, m_view.m_addrend);
}

void memory_view::memory_view_entry::check_range_address(const char *function, offs_t addrstart, offs_t addrend)
{
	check_address(function, addrstart, addrend);
	if(addrstart < m_view.m_addrstart || addrend > m_view.m_addrend)
		fatalerror("%s: The range %x-%x exceeds the view window boundaries %x-%x.\n", function, addrstart, addrend, m_view.m_addrstart, m_view.m_addrend);
}

template<int Level, int Width, int AddrShift, endianness_t Endian> void memory_view_entry_specific<Level, Width, AddrShift, Endian>::install_ram_generic(offs_t addrstart, offs_t addrend, offs_t addrmirror, read_or_write readorwrite, void *baseptr)
{
	VPRINTF("memory_view::install_ram_generic(%s-%s mirror=%s, %s, %p)\n",
			m_addrchars, addrstart, m_addrchars, addrend,
			m_addrchars, addrmirror,
			(readorwrite == read_or_write::READ) ? "read" : (readorwrite == read_or_write::WRITE) ? "write" : (readorwrite == read_or_write::READWRITE) ? "read/write" : "??",
			baseptr);

	offs_t nstart, nend, nmask, nmirror;
	check_range_optimize_mirror("install_ram_generic", addrstart, addrend, addrmirror, nstart, nend, nmask, nmirror);

	m_view.m_select_u(m_id);

	// map for read
	if (readorwrite == read_or_write::READ || readorwrite == read_or_write::READWRITE)
	{
		auto hand_r = new handler_entry_read_memory<Width, AddrShift, Endian>(m_view.m_space, baseptr);
		hand_r->set_address_info(nstart, nmask);
		r()->populate(nstart, nend, nmirror, hand_r);
	}

	// map for write
	if (readorwrite == read_or_write::WRITE || readorwrite == read_or_write::READWRITE)
	{
		auto hand_w = new handler_entry_write_memory<Width, AddrShift, Endian>(m_view.m_space, baseptr);
		hand_w->set_address_info(nstart, nmask);
		w()->populate(nstart, nend, nmirror, hand_w);
	}

	invalidate_caches(readorwrite);
}

template<int Level, int Width, int AddrShift, endianness_t Endian> void memory_view_entry_specific<Level, Width, AddrShift, Endian>::unmap_generic(offs_t addrstart, offs_t addrend, offs_t addrmirror, read_or_write readorwrite, bool quiet)
{
	VPRINTF("memory_view::unmap(%*x-%*x mirror=%*x, %s, %s)\n",
			m_addrchars, addrstart, m_addrchars, addrend,
			m_addrchars, addrmirror,
			(readorwrite == read_or_write::READ) ? "read" : (readorwrite == read_or_write::WRITE) ? "write" : (readorwrite == read_or_write::READWRITE) ? "read/write" : "??",
			quiet ? "quiet" : "normal");

	offs_t nstart, nend, nmask, nmirror;
	check_range_optimize_mirror("unmap_generic", addrstart, addrend, addrmirror, nstart, nend, nmask, nmirror);

	m_view.m_select_u(m_id);

	// read space
	if (readorwrite == read_or_write::READ || readorwrite == read_or_write::READWRITE) {
		auto handler = static_cast<handler_entry_read<Width, AddrShift, Endian> *>(quiet ? m_view.m_space->nop_r() : m_view.m_space->unmap_r());
		handler->ref();
		r()->populate(nstart, nend, nmirror, handler);
	}

	// write space
	if (readorwrite == read_or_write::WRITE || readorwrite == read_or_write::READWRITE) {
		auto handler = static_cast<handler_entry_write<Width, AddrShift, Endian> *>(quiet ? m_view.m_space->nop_w() : m_view.m_space->unmap_w());
		handler->ref();
		w()->populate(nstart, nend, nmirror, handler);
	}

	invalidate_caches(readorwrite);
}

template<int Level, int Width, int AddrShift, endianness_t Endian> void memory_view_entry_specific<Level, Width, AddrShift, Endian>::install_view(offs_t addrstart, offs_t addrend, offs_t addrmirror, memory_view &view)
{
	offs_t nstart, nend, nmask, nmirror;
	check_range_optimize_mirror("install_view", addrstart, addrend, addrmirror, nstart, nend, nmask, nmirror);

	m_view.m_select_u(m_id);

	auto handlers = view.make_handlers(*m_view.m_space, addrstart, addrend);
	r()->populate(nstart, nend, nmirror, static_cast<handler_entry_read <Width, AddrShift, Endian> *>(handlers.first));
	w()->populate(nstart, nend, nmirror, static_cast<handler_entry_write<Width, AddrShift, Endian> *>(handlers.second));
	view.make_subdispatch(key()); // Must be called after populate
}

template<int Level, int Width, int AddrShift, endianness_t Endian> memory_passthrough_handler *memory_view_entry_specific<Level, Width, AddrShift, Endian>::install_read_tap(offs_t addrstart, offs_t addrend, offs_t addrmirror, std::string name, std::function<void (offs_t offset, uX &data, uX mem_mask)> tap, memory_passthrough_handler *mph)
{
	offs_t nstart, nend, nmask, nmirror;
	check_range_optimize_mirror("install_read_tap", addrstart, addrend, addrmirror, nstart, nend, nmask, nmirror);
	if(!mph)
		mph = m_view.m_space->make_mph();

	m_view.m_select_u(m_id);

	auto handler = new handler_entry_read_tap<Width, AddrShift, Endian>(m_view.m_space, *mph, name, tap);
	r()->populate_passthrough(nstart, nend, nmirror, handler);
	handler->unref();

	invalidate_caches(read_or_write::READ);

	return mph;
}

template<int Level, int Width, int AddrShift, endianness_t Endian> memory_passthrough_handler *memory_view_entry_specific<Level, Width, AddrShift, Endian>::install_write_tap(offs_t addrstart, offs_t addrend, offs_t addrmirror, std::string name, std::function<void (offs_t offset, uX &data, uX mem_mask)> tap, memory_passthrough_handler *mph)
{
	offs_t nstart, nend, nmask, nmirror;
	check_range_optimize_mirror("install_write_tap", addrstart, addrend, addrmirror, nstart, nend, nmask, nmirror);
	if(!mph)
		mph = m_view.m_space->make_mph();

	m_view.m_select_u(m_id);

	auto handler = new handler_entry_write_tap<Width, AddrShift, Endian>(m_view.m_space, *mph, name, tap);
	w()->populate_passthrough(nstart, nend, nmirror, handler);
	handler->unref();

	invalidate_caches(read_or_write::WRITE);

	return mph;
}

template<int Level, int Width, int AddrShift, endianness_t Endian> memory_passthrough_handler *memory_view_entry_specific<Level, Width, AddrShift, Endian>::install_readwrite_tap(offs_t addrstart, offs_t addrend, offs_t addrmirror, std::string name, std::function<void (offs_t offset, uX &data, uX mem_mask)> tapr, std::function<void (offs_t offset, uX &data, uX mem_mask)> tapw, memory_passthrough_handler *mph)
{
	offs_t nstart, nend, nmask, nmirror;
	check_range_optimize_mirror("install_readwrite_tap", addrstart, addrend, addrmirror, nstart, nend, nmask, nmirror);
	if(!mph)
		mph = m_view.m_space->make_mph();

	m_view.m_select_u(m_id);

	auto rhandler = new handler_entry_read_tap <Width, AddrShift, Endian>(m_view.m_space, *mph, name, tapr);
	r() ->populate_passthrough(nstart, nend, nmirror, rhandler);
	rhandler->unref();

	auto whandler = new handler_entry_write_tap<Width, AddrShift, Endian>(m_view.m_space, *mph, name, tapw);
	w()->populate_passthrough(nstart, nend, nmirror, whandler);
	whandler->unref();

	invalidate_caches(read_or_write::READWRITE);

	return mph;
}

template<int Level, int Width, int AddrShift, endianness_t Endian> void memory_view_entry_specific<Level, Width, AddrShift, Endian>::install_device_delegate(offs_t addrstart, offs_t addrend, device_t &device, address_map_constructor &delegate, u64 unitmask, int cswidth)
{
	check_range_address("install_device_delegate", addrstart, addrend);
	address_map map(*m_view.m_space, addrstart, addrend, unitmask, cswidth, m_view.m_device, delegate);
	map.import_submaps(m_manager.machine(), device, data_width(), endianness(), addr_shift());
	populate_from_map(&map);
}

template<int Level, int Width, int AddrShift, endianness_t Endian> void memory_view_entry_specific<Level, Width, AddrShift, Endian>::install_readwrite_port(offs_t addrstart, offs_t addrend, offs_t addrmirror, std::string rtag, std::string wtag)
{
	VPRINTF("memory_view::install_readwrite_port(%*x-%*x mirror=%*x, read=\"%s\" / write=\"%s\")\n",
			m_addrchars, addrstart, m_addrchars, addrend,
			m_addrchars, addrmirror,
			rtag.empty() ? "(none)" : rtag.c_str(), wtag.empty() ? "(none)" : wtag.c_str());

	offs_t nstart, nend, nmask, nmirror;
	check_range_optimize_mirror("install_readwrite_port", addrstart, addrend, addrmirror, nstart, nend, nmask, nmirror);

	m_view.m_select_u(m_id);

	// read handler
	if (rtag != "")
	{
		// find the port
		ioport_port *port = m_view.m_device.owner()->ioport(rtag);
		if (port == nullptr)
			throw emu_fatalerror("Attempted to map non-existent port '%s' for read in space %s of device '%s'\n", rtag.c_str(), m_view.m_name, m_view.m_device.tag());

		// map the range and set the ioport
		auto hand_r = new handler_entry_read_ioport<Width, AddrShift, Endian>(m_view.m_space, port);
		r()->populate(nstart, nend, nmirror, hand_r);
	}

	if (wtag != "")
	{
		// find the port
		ioport_port *port = m_view.m_device.owner()->ioport(wtag);
		if (port == nullptr)
			fatalerror("Attempted to map non-existent port '%s' for write in space %s of device '%s'\n", wtag.c_str(), m_view.m_name, m_view.m_device.tag());

		// map the range and set the ioport
		auto hand_w = new handler_entry_write_ioport<Width, AddrShift, Endian>(m_view.m_space, port);
		w()->populate(nstart, nend, nmirror, hand_w);
	}

	invalidate_caches(rtag != "" ? wtag != "" ? read_or_write::READWRITE : read_or_write::READ : read_or_write::WRITE);
}
template<int Level, int Width, int AddrShift, endianness_t Endian> void memory_view_entry_specific<Level, Width, AddrShift, Endian>::install_bank_generic(offs_t addrstart, offs_t addrend, offs_t addrmirror, memory_bank *rbank, memory_bank *wbank)
{
	VPRINTF("memory_view::install_readwrite_bank(%*x-%*x mirror=%*x, read=\"%s\" / write=\"%s\")\n",
			m_addrchars, addrstart, m_addrchars, addrend,
			m_addrchars, addrmirror,
			(rbank != nullptr) ? rbank->tag() : "(none)", (wbank != nullptr) ? wbank->tag() : "(none)");

	offs_t nstart, nend, nmask, nmirror;
	check_range_optimize_mirror("install_bank_generic", addrstart, addrend, addrmirror, nstart, nend, nmask, nmirror);

	m_view.m_select_u(m_id);

	// map the read bank
	if (rbank != nullptr)
	{
		auto hand_r = new handler_entry_read_memory_bank<Width, AddrShift, Endian>(m_view.m_space, *rbank);
		hand_r->set_address_info(nstart, nmask);
		r()->populate(nstart, nend, nmirror, hand_r);
	}

	// map the write bank
	if (wbank != nullptr)
	{
		auto hand_w = new handler_entry_write_memory_bank<Width, AddrShift, Endian>(m_view.m_space, *wbank);
		hand_w->set_address_info(nstart, nmask);
		w()->populate(nstart, nend, nmirror, hand_w);
	}

	invalidate_caches(rbank ? wbank ? read_or_write::READWRITE : read_or_write::READ : read_or_write::WRITE);
}
