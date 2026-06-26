#pragma once

#include "../../xrCore/fixedmap.h"

//#ifndef USE_MEMORY_MONITOR
//#	define USE_DOUG_LEA_ALLOCATOR_FOR_RENDER
//#endif // USE_MEMORY_MONITOR

#ifdef USE_DOUG_LEA_ALLOCATOR_FOR_RENDER
//	extern doug_lea_allocator	g_render_lua_allocator;

	template <class T>
	class doug_lea_alloc {
	public:
		using size_type = size_t;
		using difference_type = ptrdiff_t;
		using pointer = T*;
		using const_pointer = const T*;
		using reference = T&;
		using const_reference = const T&;
		using value_type = T;

	public:
		template<class _Other>	
		struct rebind			{ using other = doug_lea_alloc<_Other>;	};
	public:
								pointer					address			(reference _Val) const					{	return (&_Val);	}
								const_pointer			address			(const_reference _Val) const			{	return (&_Val);	}
														doug_lea_alloc	()										{	}
														doug_lea_alloc	(const doug_lea_alloc<T>&)				{	}
		template<class _Other>							doug_lea_alloc	(const doug_lea_alloc<_Other>&)			{	}
		template<class _Other>	doug_lea_alloc<T>&		operator=		(const doug_lea_alloc<_Other>&)			{	return (*this);	}
								pointer					allocate		(size_type n, const void* p=0) const	{	return (T*)g_render_lua_allocator.malloc_impl(sizeof(T)*(u32)n);	}
								void					deallocate		(pointer p, size_type n) const			{	g_render_lua_allocator.free_impl	((void*&)p);				}
								void					deallocate		(void* p, size_type n) const			{	g_render_lua_allocator.free_impl	(p);				}
								char*					__charalloc		(size_type n)							{	return (char*)allocate(n); }
								void					construct		(pointer p, const T& _Val)				{	std::_Construct(p, _Val);	}
								void					destroy			(pointer p)								{	std::_Destroy(p);			}
								size_type				max_size		() const								{	size_type _Count = (size_type)(-1) / sizeof (T);	return (0 < _Count ? _Count : 1);	}
	};

	template<class _Ty,	class _Other>	inline	bool operator==(const doug_lea_alloc<_Ty>&, const doug_lea_alloc<_Other>&)		{	return (true);							}
	template<class _Ty, class _Other>	inline	bool operator!=(const doug_lea_alloc<_Ty>&, const doug_lea_alloc<_Other>&)		{	return (false);							}

	struct doug_lea_allocator_wrapper {
		template <typename T>
		struct helper {
			using result = doug_lea_alloc<T>;
		};

		static	void	*alloc		(const u32 &n)	{	return g_render_lua_allocator.malloc_impl((u32)n);	}
		template <typename T>
		static	void	dealloc		(T *&p)			{	g_render_lua_allocator.free_impl((void*&)p);	}
	};

#	define render_alloc				doug_lea_alloc
	using render_allocator = doug_lea_allocator_wrapper;

#else // USE_DOUG_LEA_ALLOCATOR_FOR_RENDER
#	define render_alloc				xalloc
using render_allocator = xr_allocator;
#endif // USE_DOUG_LEA_ALLOCATOR_FOR_RENDER

class dxRender_Visual;
struct SPass;

// #define	USE_RESOURCE_DEBUGGER

namespace R_dsgraph
{
	template<typename T, bool Reverse>
	struct DSGraphItem
	{
		T sortKey;
		float ssa = 0.0f;
		IRenderable* pObject = nullptr;
		dxRender_Visual* pVisual = nullptr;
		Fmatrix* pMatrix = nullptr;
		ShaderElement* pSE = nullptr;
		bool b_hud_mode = false;

        DSGraphItem(T key, float _ssa, IRenderable* obj, dxRender_Visual* vis,
            Fmatrix* mat, ShaderElement* se, bool hud)
            : sortKey(key), ssa(_ssa), pObject(obj), pVisual(vis),
            pMatrix(mat), pSE(se), b_hud_mode(hud) {
        }

        bool operator<(const DSGraphItem& other) const noexcept
        {
            if constexpr (Reverse)
                return other.sortKey < sortKey;
            else
                return sortKey < other.sortKey;
        }
	};

#if defined(USE_DX10) || defined(USE_DX11)	//	DX10 needs shader signature to propperly bind deometry to shader
	using vs_type = SVS*;
	using gs_type = ID3DGeometryShader*;
#ifdef USE_DX11
	using hs_type = ID3D11HullShader*;
	using ds_type = ID3D11DomainShader*;
#endif
#else	//	USE_DX10
	using vs_type = ID3DVertexShader*;
#endif	//	USE_DX10
	using ps_type = ID3DPixelShader*;

	template<typename T, bool Reverse>
	using mapDSGraphItems = xr_vector<DSGraphItem<T, Reverse>, typename render_allocator::template helper<DSGraphItem<T, Reverse>>::result>;

	template<typename T, bool Reverse>
	using mapDSGraphItemsMap = FixedMAP<T, DSGraphItem<T, Reverse>, render_allocator>;

	struct alignas(16) RenderPacketSortKey
	{
		// Calculated key for sorting
		u64 high; // VS, GS, PS, HS
		u64 low; // DS, Constants, State, Textures

		bool operator<(const RenderPacketSortKey& other) const noexcept
		{
			if (high != other.high)
				return high < other.high;
			return low < other.low;
		}

		bool operator!=(const RenderPacketSortKey& other) const noexcept
		{
			return (high != other.high) || (low != other.low);
		}

        bool operator==(const RenderPacketSortKey& other) const noexcept
        {
            return (high == other.high) && (low == other.low);
        }
	};

	struct RenderPacket
	{
		// Sorting key
        RenderPacketSortKey sortKey;

		// Visual data
		DSGraphItem<u32, false> item;

		// Pointers to resources (previously keys in FixedMAPs)
        ID3DState* pState;

#if defined(USE_DX10) || defined(USE_DX11)
		gs_type pGS;
#else
        u64 _unused_pad_gs;
#endif

#ifdef USE_DX11
		hs_type pHS;
		ds_type pDS;
#else
        u64 _unused_pad_hs, _unused_pad_ds;
#endif

        vs_type pVS;
		ps_type pPS;
        R_constant_table* pCS;
		STextureList* pTextures;

        RenderPacket(const DSGraphItem<u32, false>& _item, const SPass& pass) : item(_item)
        {
            // Extract resource pointers from shader pass (previously used as map keys)
#if defined(USE_DX10) || defined(USE_DX11)
            pVS = &*pass.vs;
            pGS = pass.gs->gs;
#else
            pVS = pass.vs->vs;
#endif

            pPS = pass.ps->ps;

#ifdef USE_DX11
            pHS = pass.hs->sh;
            pDS = pass.ds->sh;
#endif

            pCS = pass.constants._get();
            pState = pass.state->state;
            pTextures = pass.T._get();

            // Build sort key
            // Optimized grouping based on profiling, example:
            // States:4, GS:0, HS:0, DS:0 they are pretty much unused and/or unchanged
            // VS:13, PS:28, CS:93, Tex:179. Grouping based on increasing change of state
            // Low key is used just for sorting
            u64 keyHigh = 0;
            u64 keyLow = 0;

            keyHigh |= ((u64)pState >> 4 & 0xFFFF) << 48;

#if defined(USE_DX10) || defined(USE_DX11)
            keyHigh |= ((u64)pGS >> 4 & 0xFFFF) << 32;
#endif

#ifdef USE_DX11
            keyHigh |= ((u64)pHS >> 4 & 0xFFFF) << 16;
            keyHigh |= ((u64)pDS >> 4 & 0xFFFF);
#endif

            keyLow |= ((u64)pVS >> 4 & 0xFFFF) << 48;
            keyLow |= ((u64)pPS >> 4 & 0xFFFF) << 32;
            keyLow |= ((u64)pCS >> 4 & 0xFFFF) << 16;
            keyLow |= ((u64)pTextures >> 4 & 0xFFFF);

            sortKey = { keyHigh, keyLow };
        }

        bool operator<(const RenderPacket& other) const noexcept
        {
            return sortKey < other.sortKey;
        }
	};

	using RenderQueue = xr_vector<RenderPacket, render_allocator::helper<RenderPacket>::result>;
	using RenderQueueArray = xr_array<xr_array<RenderQueue, SHADER_PASSES_MAX>, 2>;

	struct DynamicSceneRgraph
	{
		template<typename T, bool Reverse1, bool Reverse2, bool Reverse3, bool Reverse4>
		struct mapSorted
		{
			mapDSGraphItems<T, Reverse1> Sorted;

			mapDSGraphItems<T, Reverse2> Wmark;
			mapDSGraphItems<T, Reverse3> Emissive;
			mapDSGraphItems<T, Reverse4> Distort;
		};

		mapSorted<float, true, false, false, true> mapStaticSorted;
		mapSorted<float, true, false, false, true> mapDynamicSorted;

		RenderQueueArray mapStaticPasses;
		RenderQueueArray mapDynamicPasses;

		mapDSGraphItems<float, false> mapHUD;
		mapSorted<float, true, false, false, false> mapHUDSorted;

		mapDSGraphItems<float, false> mapLOD;

		// Anomaly
		mapDSGraphItems<float, false> mapCamAttached;
		mapSorted<float, true, false, false, false> mapCamAttachedSorted;
		mapDSGraphItems<float, false> mapWater;
#ifdef USE_DX11
		mapDSGraphItems<float, true> mapScopeHUDSorted;
		mapDSGraphItems<float, true> mapScopeHUDObjective; // pip the FRONT (objective) scope lens, real geometry for the svpscope-2 camera, never drawn
		mapDSGraphItems<float, true> mapReflexHUDSorted; // pip reflex-sight lenses (iScopeLense==10)
		mapDSGraphItems<float, false> mapScopeHUD;
#endif
		template<bool free = true>
		IC void clear_graph(RenderQueueArray& queue, u32 _priority)
		{
			PROF_EVENT("r_dsgraph_clear_graph");
			for (u32 iPass = 0; iPass < SHADER_PASSES_MAX; ++iPass)
			{
				if constexpr (free)
					queue[_priority][iPass].clear_and_free();
				else
					queue[_priority][iPass].clear();
			}
		}

		template<bool free = true>
		IC void clear_dynamic()
		{
			clear_graph<free>(mapDynamicPasses, 0);
			clear_graph<free>(mapDynamicPasses, 1);

			if constexpr (free)
			{
				mapDynamicSorted.Wmark.clear_and_free();
				mapDynamicSorted.Emissive.clear_and_free();
				mapDynamicSorted.Sorted.clear_and_free();
				mapDynamicSorted.Distort.clear_and_free();
			}
			else
			{
				mapDynamicSorted.Wmark.clear();
				mapDynamicSorted.Emissive.clear();
				mapDynamicSorted.Sorted.clear();
				mapDynamicSorted.Distort.clear();
			}
		}

		template<bool free = true>
		IC void clear_static()
		{
			clear_graph<free>(mapStaticPasses, 0);
			clear_graph<free>(mapStaticPasses, 1);

			if constexpr (free)
			{
				mapStaticSorted.Wmark.clear_and_free();
				mapStaticSorted.Emissive.clear_and_free();
				mapStaticSorted.Sorted.clear_and_free();
				mapStaticSorted.Distort.clear_and_free();
			}
			else
			{
				mapStaticSorted.Wmark.clear();
				mapStaticSorted.Emissive.clear();
				mapStaticSorted.Sorted.clear();
				mapStaticSorted.Distort.clear();
			}
		}

		template<bool free = true>
		IC void clear_hud()
		{
			if constexpr (free)
			{
				mapHUD.clear_and_free();
				mapHUDSorted.Wmark.clear_and_free();
				mapHUDSorted.Emissive.clear_and_free();
				mapHUDSorted.Sorted.clear_and_free();
				mapHUDSorted.Distort.clear_and_free();
				mapCamAttached.clear_and_free();
				mapCamAttachedSorted.Wmark.clear_and_free();
				mapCamAttachedSorted.Emissive.clear_and_free();
				mapCamAttachedSorted.Sorted.clear_and_free();
				mapCamAttachedSorted.Distort.clear_and_free();

#ifdef USE_DX11
				mapScopeHUD.clear_and_free();
				mapScopeHUDSorted.clear_and_free();
				mapScopeHUDObjective.clear_and_free();
				mapReflexHUDSorted.clear_and_free();
#endif
			}
			else
			{
				mapHUD.clear();
				mapHUDSorted.Wmark.clear();
				mapHUDSorted.Emissive.clear();
				mapHUDSorted.Sorted.clear();
				mapHUDSorted.Distort.clear();
				mapCamAttached.clear();
				mapCamAttachedSorted.Wmark.clear();
				mapCamAttachedSorted.Emissive.clear();
				mapCamAttachedSorted.Sorted.clear();
				mapCamAttachedSorted.Distort.clear();

#ifdef USE_DX11
				mapScopeHUD.clear();
				mapScopeHUDSorted.clear();
				mapScopeHUDObjective.clear();
				mapReflexHUDSorted.clear();
#endif
			}
		}

		template<bool free = true>
		IC void clear_lods()
		{
			if constexpr (free)
				mapLOD.clear_and_free();
			else
				mapLOD.clear();
		}

		template<bool free = true>
		IC void clear()
		{
			clear_dynamic<free>();
			clear_static<free>();
			clear_hud<free>();
			clear_lods<free>();
			if constexpr (free)
				mapWater.clear_and_free();
			else
				mapWater.clear();
		}
	};
};
