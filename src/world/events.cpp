#include <world/world.hpp>
#include <new>
#include <type_traits>


namespace MCPP {


	bool WorldContainer::can_set (BlockID id, Block from, Block to) noexcept {
	
		try {
	
			return (
				CanSet(
					id,
					from,
					to
				) &&
				CanReplace[from.GetType()](
					id,
					from,
					to
				) &&
				CanPlace[to.GetType()](
					id,
					from,
					to
				)
			);
			
		} catch (...) {	}
		
		return false;
	
	}
	
	
	void WorldContainer::on_set (BlockID id, Block from, Block to) {
	
		OnSet(
			id,
			from,
			to
		);
		
		OnReplace[from.GetType()](
			id,
			from,
			to
		);
		
		OnPlace[to.GetType()](
			id,
			from,
			to
		);
	
	}
	
	
	template <Word n, typename T>
	static inline void init_array (T (& arr) [n]) noexcept(std::is_nothrow_default_constructible<T>::value) {
	
		for (auto & i : arr) new (&i) T ();
	
	}
	
	
	template <Word n, typename T>
	static inline void destroy_array (T (& arr) [n]) noexcept {
	
		for (auto & i : arr) i.~T();
	
	}
	
	
	void WorldContainer::init_events () noexcept {
	
		init_array(OnReplace);
		init_array(OnPlace);
		init_array(CanReplace);
		init_array(CanPlace);
	
	}
	
	
	void WorldContainer::destroy_events () noexcept {
	
		destroy_array(OnReplace);
		destroy_array(OnPlace);
		destroy_array(CanReplace);
		destroy_array(CanPlace);
	
	}


}