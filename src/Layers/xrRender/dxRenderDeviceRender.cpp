#include "stdafx.h"
#include "dxRenderDeviceRender.h"

#include "ResourceManager.h"
#include "../../xrEngine/shader_bus.h"

// a shader bus texture lane's $user$bus_ alias and the texture it shows
struct bus_texture_slot
{
	ref_texture alias;
	ref_texture target;
	ID3DBaseTexture* applied;
};

static xr_vector<bus_texture_slot> bus_texture_slots;
static u32 bus_texture_serial = 0;

// empties every alias so no surface outlives a reset or the device
static void bus_textures_release()
{
	for (u32 i = 0; i < bus_texture_slots.size(); ++i)
	{
		bus_texture_slots[i].alias->surface_set(nullptr);
		bus_texture_slots[i].applied = nullptr;
	}
}

// rebuilds the slots when a lane's texture changed and follows targets whose surface moved
static void bus_textures_update()
{
	const u32 serial = ShaderBus::texture_serial();
	if (serial != bus_texture_serial)
	{
		bus_texture_serial = serial;

		// the old slots keep their textures loaded until the new slots take them
		xr_vector<bus_texture_slot> old_slots;
		old_slots.swap(bus_texture_slots);

		shared_str id, name;
		for (u32 i = 0; ShaderBus::texture_get(i, id, name); ++i)
		{
			if (!name.size())
				continue;

			bus_texture_slot slot;
			slot.target.create(name.c_str());
			if (!slot.target)
				continue;

			string_path alias;
			xr_sprintf(alias, "$user$bus_%s", id.c_str());
			slot.alias.create(alias);
			slot.applied = nullptr;
			bus_texture_slots.push_back(slot);
		}

		for (u32 i = 0; i < old_slots.size(); ++i)
			old_slots[i].alias->surface_set(nullptr);
	}

	for (u32 i = 0; i < bus_texture_slots.size(); ++i)
	{
		bus_texture_slot& slot = bus_texture_slots[i];
		ID3DBaseTexture* surface = slot.target->surface_get();
		if (surface != slot.applied)
		{
			slot.alias->surface_set(surface);
			slot.applied = surface;
		}
		_RELEASE(surface);
	}
}

#if defined(USE_DX11)
CRT* bus_target_rt(const shared_str& id, bool prev);

// one staging copy of a readback block and the frame, level and request it was taken for
struct bus_readback_slot
{
	ID3D11Resource* staging;
	u32 frame;
	u32 epoch;
	u32 generation;
	bool busy;
};

// the copies in flight for one readback lane, all in the format and size of the last copy
struct bus_readback_ring
{
	ShaderBus::lane* lane;
	bus_readback_slot slots[ShaderBus::readback_ring];
	DXGI_FORMAT format;
	u32 w;
	u32 h;
	bool warned;
};

static xr_vector<bus_readback_ring> bus_readback_rings;

static void bus_readback_free(bus_readback_ring& ring)
{
	for (u32 k = 0; k < ShaderBus::readback_ring; ++k)
	{
		_RELEASE(ring.slots[k].staging);
		ring.slots[k].busy = false;
	}
}

static void bus_readback_land(ShaderBus::lane* l, u8 state, u32 frame)
{
	ShaderBus::lane_readback& r = *l->readback;
	if (state != ShaderBus::readback_ok)
		r.landed.clear();
	r.landed_state = state;
	r.landed_frame = frame;
	r.landed_new = true;
}

// the lane's ring, added on its first copy
static bus_readback_ring* bus_readback_ring_of(ShaderBus::lane* l)
{
	for (u32 k = 0; k < bus_readback_rings.size(); ++k)
		if (bus_readback_rings[k].lane == l)
			return &bus_readback_rings[k];
	bus_readback_rings.push_back(bus_readback_ring());
	bus_readback_ring* ring = &bus_readback_rings.back();
	ring->lane = l;
	return ring;
}

// copies one lane's block of res into a free staging slot of its ring
static void bus_readback_copy(ShaderBus::lane* l, ID3D11Resource* res)
{
	const ShaderBus::lane_readback& r = *l->readback;
	D3D_TEXTURE2D_DESC desc;
	static_cast<ID3D11Texture2D*>(res)->GetDesc(&desc);
	const bool readable = desc.SampleDesc.Count == 1 && (desc.Format == DXGI_FORMAT_R8G8B8A8_UNORM ||
		desc.Format == DXGI_FORMAT_R16G16B16A16_FLOAT || desc.Format == DXGI_FORMAT_R10G10B10A2_UNORM ||
		desc.Format == DXGI_FORMAT_R16G16_FLOAT || desc.Format == DXGI_FORMAT_R16_FLOAT || desc.Format == DXGI_FORMAT_R32_FLOAT ||
		desc.Format == DXGI_FORMAT_R32G32B32A32_FLOAT || desc.Format == DXGI_FORMAT_R11G11B10_FLOAT);

	bus_readback_ring* ring = bus_readback_ring_of(l);

	if (!readable)
	{
		if (!ring->warned)
			Msg("! [SHADER-BUS] lane '%s' readback refused, the source is multisampled or in format %u",
			    l->id.c_str(), u32(desc.Format));
		ring->warned = true;
		bus_readback_land(l, ShaderBus::readback_refused, Device.dwFrame);
		return;
	}

	// the block stays inside the target, centred on the texel under x, y
	const u32 w = _min(u32(r.w), desc.Width);
	const u32 h = _min(u32(r.h), desc.Height);
	const u32 left = u32(clampr(int(r.x * float(desc.Width)) - int(w / 2), 0, int(desc.Width - w)));
	const u32 top = u32(clampr(int(r.y * float(desc.Height)) - int(h / 2), 0, int(desc.Height - h)));

	if (ring->format != desc.Format || ring->w != w || ring->h != h)
	{
		bus_readback_free(*ring);
		ring->format = desc.Format;
		ring->w = w;
		ring->h = h;
	}

	bus_readback_slot* slot = nullptr;
	for (u32 k = 0; k < ShaderBus::readback_ring && !slot; ++k)
		if (!ring->slots[k].busy)
			slot = &ring->slots[k];
	if (!slot)
		return;

	if (!slot->staging)
	{
		D3D_TEXTURE2D_DESC sd;
		sd.Width = w;
		sd.Height = h;
		sd.MipLevels = 1;
		sd.ArraySize = 1;
		sd.SampleDesc.Count = 1;
		sd.SampleDesc.Quality = 0;
		sd.Format = desc.Format;
		sd.Usage = D3D_USAGE_STAGING;
		sd.BindFlags = 0;
		sd.CPUAccessFlags = D3D_CPU_ACCESS_READ;
		sd.MiscFlags = 0;
		ID3D11Texture2D* staging = nullptr;
		if (FAILED(HW.pDevice->CreateTexture2D(&sd, 0, &staging)))
			return;
		slot->staging = staging;
	}

	D3D11_BOX box = { left, top, 0, left + w, top + h, 1 };
	HW.pContext->CopySubresourceRegion(slot->staging, 0, 0, 0, 0, res, 0, &box);
	slot->frame = Device.dwFrame;
	slot->epoch = ShaderBus::level_epoch();
	slot->generation = r.generation;
	slot->busy = true;
}

static void bus_readback_copy_rows(ShaderBus::lane* l);

// copies the block of every armed lane reading this source, null reads the back buffer
void bus_readback_capture(u32 source, ID3D11Resource* res)
{
	const xr_vector<ShaderBus::lane*>& lanes = ShaderBus::readbacks();
	if (lanes.empty())
		return;
	if (Device.m_SecondViewport.IsSVPFrame() || Device.m_SecondViewport.isCamReady)
		return;

	u32 first = 0;
	while (first < lanes.size() && lanes[first]->readback->source != source)
		++first;

	ID3DTexture2D* buffer = nullptr;
	if (first < lanes.size() && !res && SUCCEEDED(HW.m_pSwapChain->GetBuffer(0, __uuidof(ID3DTexture2D), (LPVOID*)&buffer)))
		res = buffer;

	for (u32 i = first; res && i < lanes.size(); ++i)
		if (lanes[i]->readback->source == source)
			bus_readback_copy(lanes[i], res);

	_RELEASE(buffer);

	if (source != ShaderBus::readback_backbuffer)
		return;

	// targets copy after the frame's last pass, with the back buffer
	for (u32 i = 0; i < lanes.size(); ++i)
	{
		const ShaderBus::lane_readback& r = *lanes[i]->readback;
		if (r.source == ShaderBus::readback_buffer)
			bus_readback_copy_rows(lanes[i]);
		if (r.source != ShaderBus::readback_target && r.source != ShaderBus::readback_history)
			continue;
		CRT* rt = bus_target_rt(r.source_id, r.source == ShaderBus::readback_history);
		if (rt && rt->pSurface)
			bus_readback_copy(lanes[i], rt->pSurface);
	}
}

// an unsigned float of five exponent bits and m mantissa bits
static float bus_small_float(u32 v, u32 m)
{
	const u32 e = v >> m;
	const u32 f = v & ((1u << m) - 1);
	if (e == 0)
		return float(f) / float(1u << (14 + m));
	const u32 bits = (e == 31 ? 255u << 23 : (e + 112) << 23) | (f << (23 - m));
	float out;
	CopyMemory(&out, &bits, sizeof(out));
	return out;
}

// decodes the copies the GPU has finished, oldest first, without waiting on one
static void bus_readback_poll()
{
	if (bus_readback_rings.empty())
		return;

	for (u32 i = 0; i < bus_readback_rings.size();)
	{
		bus_readback_ring& ring = bus_readback_rings[i];
		ShaderBus::lane_readback& r = *ring.lane->readback;
		if (r.source == ShaderBus::readback_off)
		{
			r.landed_inflight = 0;
			bus_readback_free(ring);
			bus_readback_rings.erase(bus_readback_rings.begin() + i);
			continue;
		}
		++i;

		for (;;)
		{
			bus_readback_slot* slot = nullptr;
			for (u32 k = 0; k < ShaderBus::readback_ring; ++k)
				if (ring.slots[k].busy && (!slot || ring.slots[k].frame < slot->frame))
					slot = &ring.slots[k];
			if (!slot)
				break;

			if (slot->epoch != ShaderBus::level_epoch() || slot->generation != r.generation)
			{
				slot->busy = false;
				continue;
			}

			D3D11_MAPPED_SUBRESOURCE m;
			const HRESULT hr = HW.pContext->Map(slot->staging, 0, D3D11_MAP_READ, D3D11_MAP_FLAG_DO_NOT_WAIT, &m);
			if (hr == DXGI_ERROR_WAS_STILL_DRAWING)
				break;
			slot->busy = false;
			if (FAILED(hr))
			{
				bus_readback_land(ring.lane, ShaderBus::readback_lost, slot->frame);
				continue;
			}

			xr_vector<Fvector4>& out = r.landed;
			out.resize(ring.w * ring.h);
			for (u32 y = 0; y < ring.h; ++y)
			{
				const u8* src = (const u8*)m.pData + y * m.RowPitch;
				Fvector4* dst = &out[y * ring.w];
				switch (ring.format)
				{
				case DXGI_FORMAT_R8G8B8A8_UNORM:
					for (u32 x = 0; x < ring.w; ++x)
						dst[x].set(src[x * 4] / 255.f, src[x * 4 + 1] / 255.f, src[x * 4 + 2] / 255.f, src[x * 4 + 3] / 255.f);
					break;
				case DXGI_FORMAT_R10G10B10A2_UNORM:
					for (u32 x = 0; x < ring.w; ++x)
					{
						const u32 v = ((const u32*)src)[x];
						dst[x].set(float(v & 1023) / 1023.f, float((v >> 10) & 1023) / 1023.f,
						           float((v >> 20) & 1023) / 1023.f, float(v >> 30) / 3.f);
					}
					break;
				case DXGI_FORMAT_R32G32B32A32_FLOAT:
				case DXGI_FORMAT_UNKNOWN:
					CopyMemory(dst, src, ring.w * 16);
					break;
				case DXGI_FORMAT_R32_FLOAT:
					for (u32 x = 0; x < ring.w; ++x)
						dst[x].set(((const float*)src)[x], 0.f, 0.f, 1.f);
					break;
				case DXGI_FORMAT_R16_FLOAT:
				case DXGI_FORMAT_R16G16_FLOAT:
				{
					const u32 n = ring.format == DXGI_FORMAT_R16G16_FLOAT ? 2 : 1;
					for (u32 x = 0; x < ring.w; ++x)
					{
						float v[2] = { 0.f, 0.f };
						D3DXFloat16To32Array(v, (const D3DXFLOAT16*)src + x * n, n);
						dst[x].set(v[0], v[1], 0.f, 1.f);
					}
					break;
				}
				case DXGI_FORMAT_R11G11B10_FLOAT:
					for (u32 x = 0; x < ring.w; ++x)
					{
						const u32 v = ((const u32*)src)[x];
						dst[x].set(bus_small_float(v & 2047, 6), bus_small_float((v >> 11) & 2047, 6),
						           bus_small_float(v >> 22, 5), 1.f);
					}
					break;
				default:
					D3DXFloat16To32Array(&dst->x, (const D3DXFLOAT16*)src, ring.w * 4);
					break;
				}
			}
			HW.pContext->Unmap(slot->staging, 0);

			if (r.reduce != ShaderBus::reduce_none)
			{
				Fvector4 acc = out[0];
				for (u32 t = 1; t < out.size(); ++t)
					for (u32 c = 0; c < 4; ++c)
					{
						float& a = (&acc.x)[c];
						const float v = (&out[t].x)[c];
						switch (r.reduce)
						{
						case ShaderBus::reduce_mean: a += v; break;
						case ShaderBus::reduce_min: a = _min(a, v); break;
						case ShaderBus::reduce_max: a = _max(a, v); break;
						}
					}
				if (r.reduce == ShaderBus::reduce_mean)
					acc.div(float(out.size()));
				out.resize(1);
				out[0] = acc;
			}
			bus_readback_land(ring.lane, ShaderBus::readback_ok, slot->frame);
		}

		u8 busy = 0;
		for (u32 k = 0; k < ShaderBus::readback_ring; ++k)
			busy += ring.slots[k].busy ? 1 : 0;
		r.landed_inflight = busy;
	}
}

// drops every copy in flight so no staging outlives a reset or the device
static void bus_readback_release()
{
	for (u32 i = 0; i < bus_readback_rings.size(); ++i)
	{
		bus_readback_free(bus_readback_rings[i]);
		bus_readback_land(bus_readback_rings[i].lane, ShaderBus::readback_none, 0);
		bus_readback_rings[i].lane->readback->landed_inflight = 0;
	}
}
#endif

#if defined(USE_DX10) || defined(USE_DX11)
// a buffer lane's GPU copy, its view behind $user$busbuf_<id> or $user$busbufu_<id> and the change it holds
struct bus_buffer_slot
{
	ShaderBus::lane* lane;
	ref_texture alias;
	ID3DBuffer* buffer;
	ID3DShaderResourceView* view;
#if defined(USE_DX11)
	// the compute view of a buffer a compute pass writes, null for one the rows upload into
	ID3D11UnorderedAccessView* uav;
	// a uint buffer as one uint an element, four a row, for atomics
	ID3D11UnorderedAccessView* uav_scalar;
#endif
	bool gpu;
	bool args;
	u32 rows;
	u8 kind;
	u32 uploaded;
	u32 creates;
};

static xr_vector<bus_buffer_slot> bus_buffer_slots;

// recreates a buffer whose size or kind moved and uploads the rows of a lane that changed
static void bus_buffers_update()
{
	const xr_vector<ShaderBus::lane*>& lanes = ShaderBus::buffers();
	if (lanes.empty())
		return;

	for (u32 i = 0; i < lanes.size(); ++i)
	{
		ShaderBus::lane* l = lanes[i];
		bus_buffer_slot* slot = nullptr;
		for (u32 k = 0; k < bus_buffer_slots.size() && !slot; ++k)
			if (bus_buffer_slots[k].lane == l)
				slot = &bus_buffer_slots[k];
		if (!slot)
		{
			bus_buffer_slots.push_back(bus_buffer_slot());
			slot = &bus_buffer_slots.back();
			slot->lane = l;
		}

		if (slot->rows != l->buffer_bound || slot->kind != l->buffer_kind || slot->gpu != l->buffer_gpu || slot->args != l->buffer_args)
		{
			if (slot->alias)
				slot->alias->surface_set(nullptr);
#if defined(USE_DX11)
			_RELEASE(slot->uav);
			_RELEASE(slot->uav_scalar);
#endif
			_RELEASE(slot->view);
			_RELEASE(slot->buffer);
			slot->rows = l->buffer_bound;
			slot->kind = l->buffer_kind;
			slot->gpu = l->buffer_gpu;
			slot->args = l->buffer_args;
			slot->uploaded = l->changes - 1;
			++slot->creates;

			const bool uint_rows = l->buffer_kind == ShaderBus::kind_uint;
			string_path alias;
			xr_sprintf(alias, uint_rows ? "$user$busbufu_%s" : "$user$busbuf_%s", l->id.c_str());
			slot->alias.create(alias);

			D3D_BUFFER_DESC bd = {};
			bd.ByteWidth = l->buffer_bound * 16;
			bd.Usage = D3D_USAGE_DEFAULT;
			bd.BindFlags = D3D_BIND_SHADER_RESOURCE;
			D3D_SHADER_RESOURCE_VIEW_DESC vd = {};
			vd.Format = uint_rows ? DXGI_FORMAT_R32G32B32A32_UINT : DXGI_FORMAT_R32G32B32A32_FLOAT;
			vd.ViewDimension = D3D_SRV_DIMENSION_BUFFER;
			vd.Buffer.NumElements = l->buffer_bound;
#if defined(USE_DX11)
			if (slot->gpu)
				bd.BindFlags |= D3D11_BIND_UNORDERED_ACCESS;
			if (slot->args)
				bd.MiscFlags = D3D11_RESOURCE_MISC_DRAWINDIRECT_ARGS;
			D3D11_UNORDERED_ACCESS_VIEW_DESC ud = {};
			ud.Format = vd.Format;
			ud.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
			ud.Buffer.NumElements = l->buffer_bound;
			D3D11_UNORDERED_ACCESS_VIEW_DESC us = ud;
			us.Format = DXGI_FORMAT_R32_UINT;
			us.Buffer.NumElements = l->buffer_bound * 4;
			const bool made = SUCCEEDED(HW.pDevice->CreateBuffer(&bd, nullptr, &slot->buffer)) &&
				SUCCEEDED(HW.pDevice->CreateShaderResourceView(slot->buffer, &vd, &slot->view)) &&
				(!slot->gpu || SUCCEEDED(HW.pDevice->CreateUnorderedAccessView(slot->buffer, &ud, &slot->uav))) &&
				(!slot->gpu || !uint_rows || SUCCEEDED(HW.pDevice->CreateUnorderedAccessView(slot->buffer, &us, &slot->uav_scalar)));
#else
			const bool made = SUCCEEDED(HW.pDevice->CreateBuffer(&bd, nullptr, &slot->buffer)) &&
				SUCCEEDED(HW.pDevice->CreateShaderResourceView(slot->buffer, &vd, &slot->view));
#endif
			if (!made)
			{
				Msg("! [SHADER-BUS] lane '%s' buffer of %u rows could not be created", l->id.c_str(), l->buffer_bound);
#if defined(USE_DX11)
				_RELEASE(slot->uav);
				_RELEASE(slot->uav_scalar);
#endif
				_RELEASE(slot->view);
				_RELEASE(slot->buffer);
				continue;
			}
			slot->alias->view_set(slot->buffer, slot->view);
#if defined(USE_DX11)
			// a buffer a compute pass writes starts at zero bits on every create, in either format
			static const UINT zero[4] = {};
			if (slot->uav)
				HW.pContext->ClearUnorderedAccessViewUint(slot->uav, zero);
#endif
		}

		if (slot->buffer && !slot->gpu && slot->uploaded != l->changes && !l->rows_bound.empty())
		{
			D3D_BOX box = { 0, 0, 0, _min(u32(l->rows_bound.size()), l->buffer_bound) * 16, 1, 1 };
			HW.pContext->UpdateSubresource(slot->buffer, 0, &box, &l->rows_bound[0], 0, 0);
			slot->uploaded = l->changes;
		}
	}
}

// drops every GPU copy so none outlives a reset or the device, the next Begin recreates them
static void bus_buffers_release()
{
	for (u32 i = 0; i < bus_buffer_slots.size(); ++i)
	{
		bus_buffer_slot& slot = bus_buffer_slots[i];
		if (slot.alias)
			slot.alias->surface_set(nullptr);
#if defined(USE_DX11)
		_RELEASE(slot.uav);
		_RELEASE(slot.uav_scalar);
#endif
		_RELEASE(slot.view);
		_RELEASE(slot.buffer);
		slot.rows = 0;
	}
}

#if defined(USE_DX11)
// the UAV, rows and alias of a buffer lane, false for a lane with no buffer
bool bus_buffer_uav(const shared_str& id, bool scalar, ID3D11UnorderedAccessView*& uav, u32& rows, CTexture*& alias)
{
	for (u32 i = 0; i < bus_buffer_slots.size(); ++i)
	{
		const bus_buffer_slot& slot = bus_buffer_slots[i];
		if (!slot.rows || !slot.lane->id.equal(id))
			continue;
		uav = scalar ? slot.uav_scalar : slot.uav;
		rows = slot.rows;
		alias = slot.alias._get();
		return true;
	}
	return false;
}

// the buffer a compute pass of the owner dispatches from, null until it holds the row
ID3D11Buffer* bus_buffer_args(const shared_str& id, const shared_str& owner, u32 row)
{
	for (u32 i = 0; i < bus_buffer_slots.size(); ++i)
	{
		const bus_buffer_slot& slot = bus_buffer_slots[i];
		if (!slot.rows || !slot.lane->id.equal(id))
			continue;
		return slot.args && row < slot.rows && slot.kind == ShaderBus::kind_uint && slot.lane->owner.equal(owner) ? slot.buffer : nullptr;
	}
	return nullptr;
}

// copies the rows a lane reads from a buffer lane into a free staging slot of its ring
static void bus_readback_copy_rows(ShaderBus::lane* l)
{
	const ShaderBus::lane_readback& r = *l->readback;
	const bus_buffer_slot* buffer = nullptr;
	for (u32 i = 0; i < bus_buffer_slots.size() && !buffer; ++i)
		if (bus_buffer_slots[i].buffer && bus_buffer_slots[i].lane->id.equal(r.source_id))
			buffer = &bus_buffer_slots[i];
	if (!buffer || r.first >= buffer->rows)
		return;
	const u32 count = _min(u32(r.count), buffer->rows - r.first);

	bus_readback_ring* ring = bus_readback_ring_of(l);
	if (buffer->kind == ShaderBus::kind_uint && r.reduce != ShaderBus::reduce_none)
	{
		if (!ring->warned)
			Msg("! [SHADER-BUS] lane '%s' readback refused, a reduce of uint rows", l->id.c_str());
		ring->warned = true;
		bus_readback_land(l, ShaderBus::readback_refused, Device.dwFrame);
		return;
	}

	if (ring->format != DXGI_FORMAT_UNKNOWN || ring->w != count)
	{
		bus_readback_free(*ring);
		ring->format = DXGI_FORMAT_UNKNOWN;
		ring->w = count;
		ring->h = 1;
	}

	bus_readback_slot* slot = nullptr;
	for (u32 k = 0; k < ShaderBus::readback_ring && !slot; ++k)
		if (!ring->slots[k].busy)
			slot = &ring->slots[k];
	if (!slot)
		return;

	if (!slot->staging)
	{
		const D3D11_BUFFER_DESC sd = { count * 16, D3D11_USAGE_STAGING, 0, D3D11_CPU_ACCESS_READ, 0, 0 };
		ID3D11Buffer* staging = nullptr;
		if (FAILED(HW.pDevice->CreateBuffer(&sd, 0, &staging)))
			return;
		slot->staging = staging;
	}

	D3D11_BOX box = { r.first * 16, 0, 0, (r.first + count) * 16, 1, 1 };
	HW.pContext->CopySubresourceRegion(slot->staging, 0, 0, 0, 0, buffer->buffer, 0, &box);
	slot->frame = Device.dwFrame;
	slot->epoch = ShaderBus::level_epoch();
	slot->generation = r.generation;
	slot->busy = true;
}
#endif

// a script render target and its history copy
struct bus_target_slot
{
	ShaderBus::target_spec spec;
	ref_rt cur;
	ref_rt prev;
	bool uav;
	u32 clears;
	u32 copied_frame;
};

static xr_vector<bus_target_slot> bus_target_slots;
static u32 bus_targets_serial = u32(-1);
static u32 bus_targets_epoch = u32(-1);
static u32 bus_target_clear_serial = 0;

// the target formats in the order of ShaderBus::target_format_name and their texel bytes
static const struct
{
	D3DFORMAT format;
	u32 bytes;
} bus_target_formats[ShaderBus::target_format_count] =
{
	{ D3DFMT_A8R8G8B8, 4 },
	{ D3DFMT_A2R10G10B10, 4 },
	{ D3DFMT_V16U16, 4 },
	{ D3DFMT_G16R16F, 4 },
	{ D3DFMT_R16F, 2 },
	{ D3DFMT_R32F, 4 },
	{ D3DFMT_A16B16G16R16F, 8 },
	{ D3DFMT_A32B32G32R32F, 16 },
};

static D3DFORMAT bus_target_format(u8 format)
{
	return bus_target_formats[format].format;
}

static u32 bus_target_texel_bytes(u8 format)
{
	return bus_target_formats[format].bytes;
}

// fills the target and its history with the clear value and moves its clear count
static void bus_target_clear(bus_target_slot& s)
{
	HW.pContext->ClearRenderTargetView(s.cur->pRT, &s.spec.clear.x);
	if (s.prev)
		HW.pContext->ClearRenderTargetView(s.prev->pRT, &s.spec.clear.x);
	s.clears = ++bus_target_clear_serial;
}

// drops every target so none outlives a reset or the device, the next Begin recreates them
static void bus_targets_release()
{
	bus_target_slots.clear();
	bus_targets_serial = u32(-1);
	bus_targets_epoch = u32(-1);
}

// creates, keeps or drops a target per live spec and clears every target on a level change
static void bus_targets_update()
{
	const u32 serial = ShaderBus::targets_serial();
	const u32 epoch = ShaderBus::level_epoch();
	if (serial == bus_targets_serial && epoch == bus_targets_epoch)
		return;

	const bool level_changed = epoch != bus_targets_epoch;
	bus_targets_serial = serial;
	bus_targets_epoch = epoch;

	xr_vector<ShaderBus::target_spec> specs;
	xr_vector<ShaderBus::pass_spec> passes;
	ShaderBus::targets_copy(specs, passes);

	// slots left in the old list when it goes out of scope are destroyed
	xr_vector<bus_target_slot> old_slots;
	old_slots.swap(bus_target_slots);

	for (u32 i = 0; i < specs.size(); ++i)
	{
		const ShaderBus::target_spec& t = specs[i];
		ShaderBus::gpu_status status = {};
		if (t.released)
		{
			status.state = ShaderBus::gpu_released;
			ShaderBus::set_gpu_status(t.id, status);
			continue;
		}

		u32 w = t.width, h = t.height;
		if (t.size != ShaderBus::size_fixed)
		{
			const u32 shift = t.size == ShaderBus::size_half ? 1 : t.size == ShaderBus::size_quarter ? 2 : 0;
			w = _max(Device.dwWidth >> shift, 1u);
			h = _max(Device.dwHeight >> shift, 1u);
		}

		bus_target_slot* old = nullptr;
		for (u32 k = 0; k < old_slots.size() && !old; ++k)
			if (old_slots[k].spec.id == t.id)
				old = &old_slots[k];

		// a compute writer of the same owner needs the target as a UAV, a disabled one still counts
		bool uav = false;
		for (u32 k = 0; k < passes.size() && !uav; ++k)
			if (!passes[k].released && passes[k].cs.size() && passes[k].owner == t.owner)
				for (u32 o = 0; o < passes[k].output_count; ++o)
					uav = uav || passes[k].outputs[o] == t.id;

		if (old && old->spec.generation == t.generation && old->cur->dwWidth == w && old->cur->dwHeight == h &&
			old->uav == uav)
		{
			bus_target_slots.push_back(*old);
			if (level_changed)
				bus_target_clear(bus_target_slots.back());
		}
		else
		{
			// the old surfaces go first so the names are free for the new size
			if (old)
			{
				old->cur.destroy();
				old->prev.destroy();
			}

			bus_target_slot s = {};
			s.spec = t;
			string_path name;
			xr_sprintf(name, "$user$bus_%s", t.id.c_str());
			s.uav = uav;
#ifdef USE_DX11
			s.cur.create(name, w, h, bus_target_format(t.format), 1, uav);
#else
			s.cur.create(name, w, h, bus_target_format(t.format));
#endif
			if (t.history)
			{
				xr_strcat(name, "_prev");
				s.prev.create(name, w, h, bus_target_format(t.format));
			}
			bus_target_clear(s);
			bus_target_slots.push_back(s);
		}

		status.state = ShaderBus::gpu_ready;
		status.width = w;
		status.height = h;
		status.bytes = w * h * bus_target_texel_bytes(t.format) * (t.history ? 2 : 1);
		ShaderBus::set_gpu_status(t.id, status);
	}
}

static bus_target_slot* bus_target_find(const shared_str& id)
{
	for (u32 i = 0; i < bus_target_slots.size(); ++i)
		if (bus_target_slots[i].spec.id == id)
			return &bus_target_slots[i];
	return nullptr;
}

// the target of a lane id, prev for its history copy, null for none
CRT* bus_target_rt(const shared_str& id, bool prev)
{
	bus_target_slot* s = bus_target_find(id);
	return s ? (prev ? s->prev._get() : s->cur._get()) : nullptr;
}

// copies the target into its history once per main frame, before its first writer
void bus_target_rotate(const shared_str& id)
{
	bus_target_slot* s = bus_target_find(id);
	if (!s || !s->cur || !s->prev || !s->cur->pSurface || !s->prev->pSurface)
		return;
	if (s->copied_frame == Device.dwFrame || Device.m_SecondViewport.IsSVPFrame())
		return;

	RCache.set_RT(nullptr);
	RCache.unbind_texture(&*s->prev->pTexture);
	HW.pContext->CopyResource(s->prev->pSurface, s->cur->pSurface);
	s->copied_frame = Device.dwFrame;
}

// moves on every clear of the target or create of the buffer, 0 for none
u32 bus_target_clears(const shared_str& id)
{
	bus_target_slot* s = bus_target_find(id);
#if defined(USE_DX11)
	for (u32 i = 0; i < bus_buffer_slots.size() && !s; ++i)
		if (bus_buffer_slots[i].lane->id.equal(id))
			return bus_buffer_slots[i].creates;
#endif
	return s ? s->clears : 0;
}

void bus_passes_update();
void bus_passes_release();
#endif

dxRenderDeviceRender::dxRenderDeviceRender()
	: Resources(0)
{
	;
}

void dxRenderDeviceRender::Copy(IRenderDeviceRender& _in)
{
	*this = *(dxRenderDeviceRender*)&_in;
}

void dxRenderDeviceRender::setGamma(float fGamma)
{
	m_Gamma.Gamma(fGamma);
}

void dxRenderDeviceRender::setBrightness(float fGamma)
{
	m_Gamma.Brightness(fGamma);
}

void dxRenderDeviceRender::setContrast(float fGamma)
{
	m_Gamma.Contrast(fGamma);
}

void dxRenderDeviceRender::updateGamma()
{
	m_Gamma.Update();
}

void dxRenderDeviceRender::OnDeviceDestroy(BOOL bKeepTextures)
{
	m_WireShader.destroy();
	m_SelectionShader.destroy();

	bus_textures_release();
#if defined(USE_DX10) || defined(USE_DX11)
	bus_buffers_release();
	bus_buffer_slots.clear();
#endif
#if defined(USE_DX11)
	bus_readback_release();
	bus_readback_rings.clear();
#endif
#if defined(USE_DX10) || defined(USE_DX11)
	bus_targets_release();
	bus_passes_release();
#endif
	bus_texture_slots.clear();
	bus_texture_serial = 0;
	Resources->OnDeviceDestroy(bKeepTextures);
	RCache.OnDeviceDestroy();
}

void dxRenderDeviceRender::ValidateHW()
{
	HW.Validate();
}

void dxRenderDeviceRender::DestroyHW()
{
	xr_delete(Resources);
	HW.DestroyDevice();
}

void dxRenderDeviceRender::Reset(HWND hWnd, u32& dwWidth, u32& dwHeight, float& fWidth_2, float& fHeight_2)
{
#ifdef DEBUG
    _SHOW_REF("*ref -CRenderDevice::ResetTotal: DeviceREF:",HW.pDevice);
#endif // DEBUG

	bus_textures_release();
#if defined(USE_DX10) || defined(USE_DX11)
	bus_buffers_release();
#endif
#if defined(USE_DX11)
	bus_readback_release();
#endif
#if defined(USE_DX10) || defined(USE_DX11)
	bus_targets_release();
	bus_passes_release();
#endif
	Resources->reset_begin();
	Memory.mem_compact();

#if !defined(USE_DX10) && !defined(USE_DX11)
	const bool noTexturesInRAM = RImplementation.o.no_ram_textures;
	if (noTexturesInRAM)
		ResourcesDeferredUnload();
#endif

	HW.Reset(hWnd);

#if defined(USE_DX11)
	dwWidth = HW.m_ChainDesc.Width;
	dwHeight = HW.m_ChainDesc.Height;
#elif defined(USE_DX10)
	dwWidth = HW.m_ChainDesc.BufferDesc.Width;
	dwHeight = HW.m_ChainDesc.BufferDesc.Height;
#else	//	USE_DX10
	if (noTexturesInRAM)
		ResourcesDeferredUpload();

	dwWidth = HW.DevPP.BackBufferWidth;
	dwHeight = HW.DevPP.BackBufferHeight;
#endif	//	USE_DX10

	fWidth_2 = float(dwWidth / 2);
	fHeight_2 = float(dwHeight / 2);
	Resources->reset_end();

#ifdef DEBUG
    _SHOW_REF("*ref +CRenderDevice::ResetTotal: DeviceREF:",HW.pDevice);
#endif // DEBUG
}

void dxRenderDeviceRender::SetupStates()
{
	HW.Caps.Update();

#if defined(USE_DX10) || defined(USE_DX11)
	//	TODO: DX10: Implement Resetting of render states into default mode
	//VERIFY(!"dxRenderDeviceRender::SetupStates not implemented.");
	SSManager.SetMaxAnisotropy(ps_r__tf_Anisotropic);
	SSManager.SetMipLODBias(ps_r__tf_Mipbias);
#else	//	USE_DX10
	for (u32 i = 0; i < HW.Caps.raster.dwStages; i++)
	{
		CHK_DX(HW.pDevice->SetSamplerState(i, D3DSAMP_MAXANISOTROPY, ps_r__tf_Anisotropic));
		CHK_DX(HW.pDevice->SetSamplerState(i, D3DSAMP_MIPMAPLODBIAS, *(LPDWORD)&ps_r__tf_Mipbias));
		CHK_DX(HW.pDevice->SetSamplerState ( i, D3DSAMP_MINFILTER, D3DTEXF_LINEAR ));
		CHK_DX(HW.pDevice->SetSamplerState ( i, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR ));
		CHK_DX(HW.pDevice->SetSamplerState ( i, D3DSAMP_MIPFILTER, D3DTEXF_LINEAR ));
	}
	CHK_DX(HW.pDevice->SetRenderState( D3DRS_DITHERENABLE, TRUE ));
	CHK_DX(HW.pDevice->SetRenderState( D3DRS_COLORVERTEX, TRUE ));
	CHK_DX(HW.pDevice->SetRenderState( D3DRS_ZENABLE, TRUE ));
	CHK_DX(HW.pDevice->SetRenderState( D3DRS_SHADEMODE, D3DSHADE_GOURAUD ));
	CHK_DX(HW.pDevice->SetRenderState( D3DRS_CULLMODE, D3DCULL_CCW ));
	CHK_DX(HW.pDevice->SetRenderState( D3DRS_ALPHAFUNC, D3DCMP_GREATER ));
	CHK_DX(HW.pDevice->SetRenderState( D3DRS_LOCALVIEWER, TRUE ));

	CHK_DX(HW.pDevice->SetRenderState( D3DRS_DIFFUSEMATERIALSOURCE, D3DMCS_MATERIAL ));
	CHK_DX(HW.pDevice->SetRenderState( D3DRS_SPECULARMATERIALSOURCE,D3DMCS_MATERIAL ));
	CHK_DX(HW.pDevice->SetRenderState( D3DRS_AMBIENTMATERIALSOURCE, D3DMCS_MATERIAL ));
	CHK_DX(HW.pDevice->SetRenderState( D3DRS_EMISSIVEMATERIALSOURCE,D3DMCS_COLOR1 ));
	CHK_DX(HW.pDevice->SetRenderState( D3DRS_MULTISAMPLEANTIALIAS, FALSE ));
	CHK_DX(HW.pDevice->SetRenderState( D3DRS_NORMALIZENORMALS, TRUE ));

	if (psDeviceFlags.test(rsWireframe))
		RCache.set_FillMode(D3DFILL_WIREFRAME);
	else
		RCache.set_FillMode(D3DFILL_SOLID);

	// ******************** Fog parameters
	CHK_DX(HW.pDevice->SetRenderState( D3DRS_FOGCOLOR, 0 ));
	CHK_DX(HW.pDevice->SetRenderState( D3DRS_RANGEFOGENABLE, FALSE ));
	if (HW.Caps.bTableFog)
	{
		CHK_DX(HW.pDevice->SetRenderState( D3DRS_FOGTABLEMODE, D3DFOG_LINEAR ));
		CHK_DX(HW.pDevice->SetRenderState( D3DRS_FOGVERTEXMODE, D3DFOG_NONE ));
	}
	else
	{
		CHK_DX(HW.pDevice->SetRenderState( D3DRS_FOGTABLEMODE, D3DFOG_NONE ));
		CHK_DX(HW.pDevice->SetRenderState( D3DRS_FOGVERTEXMODE, D3DFOG_LINEAR ));
	}

#endif	//	USE_DX10
}

void dxRenderDeviceRender::OnDeviceCreate(LPCSTR shName)
{
	// Signal everyone - device created
	RCache.OnDeviceCreate();
	m_Gamma.Update();
	Resources->OnDeviceCreate(shName);
	::Render->create();
	Device.Statistic->OnDeviceCreate();
#if defined(USE_DX11)
	ShaderBus::enable_gpu_lanes();
#endif
#if defined(USE_DX10) || defined(USE_DX11)
	ShaderBus::enable_buffers();
	ShaderBus::enable_mask();
	ShaderBus::set_mask_msaa(RImplementation.o.dx10_msaa);
	ShaderBus::set_enable_targets(true);
#endif

	//#ifndef DEDICATED_SERVER
	if (!g_dedicated_server)
	{
		m_WireShader.create("editor\\wire");
		m_SelectionShader.create("editor\\selection");

		DUImpl.OnDeviceCreate();
	}
	//#endif
}

void dxRenderDeviceRender::Create(HWND hWnd, u32& dwWidth, u32& dwHeight, float& fWidth_2, float& fHeight_2,
                                  bool move_window)
{
	HW.CreateDevice(hWnd, move_window);
#if defined(USE_DX11)
	dwWidth = HW.m_ChainDesc.Width;
	dwHeight = HW.m_ChainDesc.Height;
#elif defined(USE_DX10)
	dwWidth = HW.m_ChainDesc.BufferDesc.Width;
	dwHeight = HW.m_ChainDesc.BufferDesc.Height;
#else	//	USE_DX10
	dwWidth = HW.DevPP.BackBufferWidth;
	dwHeight = HW.DevPP.BackBufferHeight;
#endif	//	USE_DX10
	fWidth_2 = float(dwWidth / 2);
	fHeight_2 = float(dwHeight / 2);
	Resources = xr_new<CResourceManager>();
}

void dxRenderDeviceRender::SetupGPU(BOOL bForceGPU_SW, BOOL bForceGPU_NonPure, BOOL bForceGPU_REF)
{
	HW.Caps.bForceGPU_SW = bForceGPU_SW;
	HW.Caps.bForceGPU_NonPure = bForceGPU_NonPure;
	HW.Caps.bForceGPU_REF = bForceGPU_REF;
}

void dxRenderDeviceRender::overdrawBegin()
{
#if defined(USE_DX10) || defined(USE_DX11)
	//	TODO: DX10: Implement overdrawBegin
	VERIFY(!"dxRenderDeviceRender::overdrawBegin not implemented.");
#else	//	USE_DX10
	// Turn stenciling
	CHK_DX(HW.pDevice->SetRenderState( D3DRS_STENCILENABLE, TRUE ));
	CHK_DX(HW.pDevice->SetRenderState( D3DRS_STENCILFUNC, D3DCMP_ALWAYS ));
	CHK_DX(HW.pDevice->SetRenderState( D3DRS_STENCILREF, 0 ));
	CHK_DX(HW.pDevice->SetRenderState( D3DRS_STENCILMASK, 0x00000000 ));
	CHK_DX(HW.pDevice->SetRenderState( D3DRS_STENCILWRITEMASK, 0xffffffff ));

	// Increment the stencil buffer for each pixel drawn
	CHK_DX(HW.pDevice->SetRenderState( D3DRS_STENCILFAIL, D3DSTENCILOP_KEEP ));
	CHK_DX(HW.pDevice->SetRenderState( D3DRS_STENCILPASS, D3DSTENCILOP_INCRSAT ));

	if (1 == HW.Caps.SceneMode)
	{
		CHK_DX(HW.pDevice->SetRenderState( D3DRS_STENCILZFAIL, D3DSTENCILOP_KEEP ));
	} // Overdraw
	else
	{
		CHK_DX(HW.pDevice->SetRenderState( D3DRS_STENCILZFAIL, D3DSTENCILOP_INCRSAT ));
	} // ZB access
#endif	//	USE_DX10
}

void dxRenderDeviceRender::overdrawEnd()
{
#if defined(USE_DX10) || defined(USE_DX11)
	//	TODO: DX10: Implement overdrawEnd
	VERIFY(!"dxRenderDeviceRender::overdrawBegin not implemented.");
#else	//	USE_DX10
	// Set up the stencil states
	CHK_DX(HW.pDevice->SetRenderState( D3DRS_STENCILZFAIL, D3DSTENCILOP_KEEP ));
	CHK_DX(HW.pDevice->SetRenderState( D3DRS_STENCILFAIL, D3DSTENCILOP_KEEP ));
	CHK_DX(HW.pDevice->SetRenderState( D3DRS_STENCILPASS, D3DSTENCILOP_KEEP ));
	CHK_DX(HW.pDevice->SetRenderState( D3DRS_STENCILFUNC, D3DCMP_EQUAL ));
	CHK_DX(HW.pDevice->SetRenderState( D3DRS_STENCILMASK, 0xff ));

	// Set the background to black
	CHK_DX(HW.pDevice->Clear(0,0,D3DCLEAR_TARGET,D3DCOLOR_XRGB(255,0,0),0,0));

	// Draw a rectangle wherever the count equal I
	RCache.OnFrameEnd();
	CHK_DX(HW.pDevice->SetFVF( FVF::F_TL ));

	// Render gradients
	for (int I = 0; I < 12; I++)
	{
		u32 _c = I * 256 / 13;
		u32 c = D3DCOLOR_XRGB(_c, _c, _c);

		FVF::TL pv[4];
		pv[0].set(float(0), float(Device.dwHeight), c, 0, 0);
		pv[1].set(float(0), float(0), c, 0, 0);
		pv[2].set(float(Device.dwWidth), float(Device.dwHeight), c, 0, 0);
		pv[3].set(float(Device.dwWidth), float(0), c, 0, 0);

		CHK_DX(HW.pDevice->SetRenderState ( D3DRS_STENCILREF, I ));
		CHK_DX(HW.pDevice->DrawPrimitiveUP ( D3DPT_TRIANGLESTRIP, 2, pv, sizeof(FVF::TL) ));
	}
	CHK_DX(HW.pDevice->SetRenderState( D3DRS_STENCILENABLE, FALSE ));
#endif	//	USE_DX10
}

void dxRenderDeviceRender::DeferredLoad(BOOL E)
{
	Resources->DeferredLoad(E);
}

void dxRenderDeviceRender::ResourcesDeferredUpload()
{
	Resources->DeferredUpload();
}

void dxRenderDeviceRender::ResourcesDeferredUnload()
{
	Resources->DeferredUnload();
}

void dxRenderDeviceRender::ResourcesPrefetchCreateTexture(LPCSTR name)
{
	Resources->_CreateTexture(name);
}

void dxRenderDeviceRender::ResourcesGetMemoryUsage(u32& m_base, u32& c_base, u32& m_lmaps, u32& c_lmaps)
{
	if (Resources)
		Resources->_GetMemoryUsage(m_base, c_base, m_lmaps, c_lmaps);
}

void dxRenderDeviceRender::ResourcesStoreNecessaryTextures()
{
	dxRenderDeviceRender::Instance().Resources->StoreNecessaryTextures();
}

void dxRenderDeviceRender::ResourcesDumpMemoryUsage()
{
	dxRenderDeviceRender::Instance().Resources->_DumpMemoryUsage();
}

dxRenderDeviceRender::DeviceState dxRenderDeviceRender::GetDeviceState()
{
	HW.Validate();
#if defined(USE_DX10) || defined(USE_DX11)
    HRESULT _hr = HW.m_pSwapChain->Present(0, DXGI_PRESENT_TEST);

	if (FAILED(_hr))
	{
		//LV: Check if it's correct. If so - that should be fix for AMD issues. Thank me later. Yo
		// If the device was lost, do not render until we get it back
		if (DXGI_ERROR_DEVICE_REMOVED == _hr)
			return dsLost;

		// Check if the device is ready to be reset
		if (DXGI_ERROR_DEVICE_RESET == _hr)
			return dsNeedReset;
	}
#else	//	USE_DX10
	HRESULT _hr = HW.pDevice->TestCooperativeLevel();

	if (FAILED(_hr))
	{
		// If the device was lost, do not render until we get it back
		if (D3DERR_DEVICELOST == _hr)
			return dsLost;

		// Check if the device is ready to be reset
		if (D3DERR_DEVICENOTRESET == _hr)
			return dsNeedReset;
	}
#endif	//	USE_DX10

	return dsOK;
}

BOOL dxRenderDeviceRender::GetForceGPU_REF()
{
	return HW.Caps.bForceGPU_REF;
}

u32 dxRenderDeviceRender::GetCacheStatPolys()
{
	return RCache.stat.polys;
}

void dxRenderDeviceRender::Begin()
{
#if !defined(USE_DX10) && !defined(USE_DX11)
	CHK_DX(HW.pDevice->BeginScene());
#endif	//	USE_DX10
	RCache.OnFrameBegin();
	RCache.set_CullMode(CULL_CW);
	RCache.set_CullMode(CULL_CCW);
	if (HW.Caps.SceneMode) overdrawBegin();
	bus_textures_update();
#if defined(USE_DX10) || defined(USE_DX11)
	bus_buffers_update();
	bus_targets_update();
	bus_passes_update();
#endif
}

void dxRenderDeviceRender::Clear()
{
#if defined(USE_DX10) || defined(USE_DX11)
	HW.pContext->ClearDepthStencilView(RCache.get_ZB(),
	                                   D3D_CLEAR_DEPTH | D3D_CLEAR_STENCIL, 1.0f, 0);

	if (psDeviceFlags.test(rsClearBB))
	{
		FLOAT ColorRGBA[4] = {0.0f, 0.0f, 0.0f, 0.0f};
		HW.pContext->ClearRenderTargetView(RCache.get_RT(), ColorRGBA);
	}
#else	//	USE_DX10
	CHK_DX(HW.pDevice->Clear(0,0,
		D3DCLEAR_ZBUFFER|
		(psDeviceFlags.test(rsClearBB)?D3DCLEAR_TARGET:0)|
		(HW.Caps.bStencil?D3DCLEAR_STENCIL:0),
		D3DCOLOR_XRGB(0,0,0),1,0
	));
#endif	//	USE_DX10
}

void DoAsyncScreenshot();

void dxRenderDeviceRender::End()
{
	VERIFY(HW.pDevice);

	if (HW.Caps.SceneMode) overdrawEnd();

	RCache.OnFrameEnd();
	Memory.dbg_check();

	DoAsyncScreenshot();

#if defined(USE_DX10) || defined(USE_DX11)
    UINT present_flags = 0;
	bool use_vsync = !!psDeviceFlags.test(rsVSync);
	UINT present_interval = (use_vsync) ? 1 : 0;

# if defined(USE_DX11)
	// NOTE: https://learn.microsoft.com/en-us/windows/win32/direct3ddxgi/variable-refresh-rate-displays
    BOOL is_windowed = HW.m_ChainDescFullscreen.Windowed;
	if (is_windowed && !use_vsync && HW.m_SupportsVRR) {
        present_flags |= DXGI_PRESENT_ALLOW_TEARING;
	}
# endif

	if (!Device.m_SecondViewport.IsSVPFrame() && !Device.m_SecondViewport.isCamReady) {
		HW.m_pSwapChain->Present(present_interval, present_flags);
	}
#else //!USE_DX10 || USE_DX11
	CHK_DX(HW.pDevice->EndScene());

	if (!Device.m_SecondViewport.IsSVPFrame() && !Device.m_SecondViewport.isCamReady)
		HW.pDevice->Present(NULL, NULL, NULL, NULL);
#endif //-USE_DX10
#if defined(USE_DX11)
	bus_readback_poll();
#endif
	//HRESULT _hr		= HW.pDevice->Present( NULL, NULL, NULL, NULL );
	//if				(D3DERR_DEVICELOST==_hr)	return;			// we will handle this later
}

void dxRenderDeviceRender::ResourcesDestroyNecessaryTextures()
{
	Resources->DestroyNecessaryTextures();
}

void dxRenderDeviceRender::ClearTarget()
{
#if defined(USE_DX10) || defined(USE_DX11)
	FLOAT ColorRGBA[4] = {0.0f, 0.0f, 0.0f, 0.0f};
	HW.pContext->ClearRenderTargetView(RCache.get_RT(), ColorRGBA);
#else	//	USE_DX10
	CHK_DX(HW.pDevice->Clear(0,0,D3DCLEAR_TARGET,D3DCOLOR_XRGB(0,0,0),1,0));
#endif	//	USE_DX10
}

void dxRenderDeviceRender::SetCacheXform(Fmatrix& mView, Fmatrix& mProject)
{
	RCache.set_xform_view(mView);
	RCache.set_xform_project(mProject);
}

void dxRenderDeviceRender::SetCacheXform_prev(Fmatrix& mView, Fmatrix& mProject)
{
	RCache.set_xform_view_prev(mView);
	RCache.set_xform_project_prev(mProject);
}

bool dxRenderDeviceRender::HWSupportsShaderYUV2RGB()
{
	u32 v_dev = CAP_VERSION(HW.Caps.raster_major, HW.Caps.raster_minor);
	u32 v_need = CAP_VERSION(2, 0);
	return (v_dev >= v_need);
}

void dxRenderDeviceRender::OnAssetsChanged()
{
	Resources->m_textures_description.UnLoad();
	Resources->m_textures_description.Load();
}

extern ENGINE_API void SetStartupMonitor(HMONITOR h);
extern XRAPI_API xr_token* vid_mode_token;
#if defined(USE_DX11) || defined(USE_DX10)
void fill_vid_mode_list(CHW* _hw);
void free_vid_mode_list();
#endif

// Windowed: keep user's pick if present in vid_mode_token, else monitor native.
// Borderless/fullscreen: always monitor native.  Moves the window to match.
// Caller must have set HW.m_pOutput / vid_mode_token to reflect the target
// monitor before calling.
static void FinalizeMonitorGeometry(const MONITORINFO& mi, HWND hWnd,
                                    u32 g_screenmode_,
                                    u32& vidModeW, u32& vidModeH)
{
    const int monX = mi.rcMonitor.left;
    const int monY = mi.rcMonitor.top;
    const int monW = mi.rcMonitor.right  - mi.rcMonitor.left;
    const int monH = mi.rcMonitor.bottom - mi.rcMonitor.top;

    u32 finalW = (u32)monW;
    u32 finalH = (u32)monH;
    if (g_screenmode_ == 0)
    {
        string32 cur_buf;
        xr_sprintf(cur_buf, sizeof(cur_buf), "%ux%u", vidModeW, vidModeH);
        for (xr_token* t = vid_mode_token; t && t->name; ++t)
        {
            if (!xr_strcmp(t->name, cur_buf))
            {
                finalW = vidModeW;
                finalH = vidModeH;
                break;
            }
        }
    }
    vidModeW = finalW;
    vidModeH = finalH;

    int wx, wy, ww = (int)finalW, wh = (int)finalH;
    if (g_screenmode_ == 0)
    {
        wx = monX + (monW - ww) / 2;
        wy = monY + (monH - wh) / 2;
    }
    else
    {
        wx = monX;
        wy = monY;
    }
    SetWindowPos(hWnd, HWND_TOP, wx, wy, ww, wh,
                 SWP_FRAMECHANGED | SWP_NOCOPYBITS | SWP_DRAWFRAME);
}

bool dxRenderDeviceRender::SwitchOutputMonitor(HMONITOR hTargetMon, HWND hWnd,
                                               u32 g_screenmode_,
                                               u32& vidModeW, u32& vidModeH)
{
    if (hTargetMon == NULL)
        return false;

    MONITORINFO mi;
    mi.cbSize = sizeof(mi);
    if (!GetMonitorInfoA(hTargetMon, &mi))
    {
        Msg("! vid_monitor: GetMonitorInfoA failed for target monitor");
        return false;
    }

#if defined(USE_DX11) || defined(USE_DX10)
    IDXGIOutput* new_output = HW.FindOutputOnCurrentAdapter(hTargetMon);
    if (!new_output)
    {
        Msg("! vid_monitor: target monitor is not on the current adapter, restart to apply");
        return false;
    }

    SetStartupMonitor(hTargetMon);

    if (g_screenmode_ == 2)
        HW.m_pSwapChain->SetFullscreenState(FALSE, NULL); // best-effort

    _RELEASE(HW.m_pOutput);
    HW.m_pOutput = new_output;

    free_vid_mode_list();
    fill_vid_mode_list(&HW);

    FinalizeMonitorGeometry(mi, hWnd, g_screenmode_, vidModeW, vidModeH);
    Msg("* vid_monitor: output swapped, final mode %ux%u", vidModeW, vidModeH);
    return true;

#elif !defined(USE_DX10) && !defined(USE_DX11)
    UINT target_adapter = UINT(-1);
    for (UINT a = 0; a < HW.pD3D->GetAdapterCount(); ++a)
    {
        if (HW.pD3D->GetAdapterMonitor(a) == hTargetMon)
        {
            target_adapter = a;
            break;
        }
    }
    if (target_adapter == UINT(-1))
    {
        Msg("! vid_monitor: target monitor not found among D3D9 adapters, restart to apply");
        return false;
    }

    if (target_adapter != HW.DevAdapter)
    {
        D3DADAPTER_IDENTIFIER9 id_cur, id_new;
        HRESULT hr1 = HW.pD3D->GetAdapterIdentifier(HW.DevAdapter, 0, &id_cur);
        HRESULT hr2 = HW.pD3D->GetAdapterIdentifier(target_adapter,  0, &id_new);
        const bool same_gpu = SUCCEEDED(hr1) && SUCCEEDED(hr2)
                           && memcmp(&id_cur.DeviceIdentifier,
                                     &id_new.DeviceIdentifier,
                                     sizeof(GUID)) == 0;
        if (!same_gpu)
        {
            Msg("! vid_monitor: target monitor is on a different D3D9 adapter, restart to apply");
            return false;
        }
    }
    if (g_screenmode_ == 2)
    {
        Msg("! vid_monitor: DX9 exclusive fullscreen across monitors requires restart to apply");
        return false;
    }

    SetStartupMonitor(hTargetMon);

    FinalizeMonitorGeometry(mi, hWnd, g_screenmode_, vidModeW, vidModeH);
    Msg("* vid_monitor: DX9 output swapped, final mode %ux%u", vidModeW, vidModeH);
    return true;

#else
    return false;
#endif
}
