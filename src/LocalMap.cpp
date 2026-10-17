#include "LocalMap.h"
#include "Extras.h"

namespace companion::localmap
{
	namespace
	{
		constexpr float kRevealRadius = 1500.0f;     // how far around you counts as explored (~21 m)
		constexpr float kExteriorRadius = 9000.0f;   // outdoors: loaded cells within this distance

		struct Mesh
		{
			RE::FormID             id{ 0 };
			std::vector<float>     cx, cy;  // triangle centers, for marking explored floor
			std::size_t            triOffset{ 0 };
		};

		std::unordered_map<RE::FormID, std::vector<bool>> g_explored;  // per navmesh, this session only
		std::vector<Mesh>                                g_meshes;     // navmeshes in the current area
		std::string                                      g_areaKey;
		std::uint32_t                                    g_version = 1;
		bool                                             g_exploredChanged = false;

		std::string Str(const char* a_text) { return a_text ? std::string(a_text) : std::string(); }

		std::vector<RE::TESObjectCELL*> AreaCells(RE::PlayerCharacter* a_player, std::string& a_key)
		{
			std::vector<RE::TESObjectCELL*> cells;
			const auto cell = a_player->GetParentCell();
			if (!cell) {
				return cells;
			}
			if (cell->IsInteriorCell()) {
				cells.push_back(cell);
				a_key = std::format("int:{:X}", cell->GetFormID());
				return cells;
			}
			if (const auto tes = RE::TES::GetSingleton(); tes) {
				tes->ForEachCellInRange(a_player, kExteriorRadius, [&](RE::TESObjectCELL* a_cell) {
					if (a_cell && a_cell->IsAttached()) {
						cells.push_back(a_cell);
					}
				});
			}
			std::sort(cells.begin(), cells.end());
			a_key = "ext";
			for (const auto c : cells) {
				a_key += std::format(":{:X}", c->GetFormID());
			}
			return cells;
		}

		std::vector<RE::NavMesh*> MeshesIn(const std::vector<RE::TESObjectCELL*>& a_cells)
		{
			std::vector<RE::NavMesh*> out;
			for (const auto cell : a_cells) {
				const auto arr = cell->GetRuntimeData().navMeshes;
				if (!arr) {
					continue;
				}
				for (const auto& mesh : arr->navMeshes) {
					if (mesh) {
						out.push_back(mesh.get());
					}
				}
			}
			return out;
		}

		void RefreshArea(RE::PlayerCharacter* a_player)
		{
			std::string key;
			const auto cells = AreaCells(a_player, key);
			if (key == g_areaKey) {
				return;
			}
			g_areaKey = key;
			g_meshes.clear();
			std::size_t offset = 0;
			for (const auto mesh : MeshesIn(cells)) {
				Mesh m;
				m.id = mesh->GetFormID();
				m.triOffset = offset;
				const auto& verts = mesh->vertices;
				for (const auto& tri : mesh->triangles) {
					float x = 0, y = 0;
					for (int k = 0; k < 3; ++k) {
						const auto vi = tri.vertices[k];
						if (vi < verts.size()) {
							x += verts[vi].location.x;
							y += verts[vi].location.y;
						}
					}
					m.cx.push_back(x / 3.0f);
					m.cy.push_back(y / 3.0f);
				}
				offset += m.cx.size();
				auto& ex = g_explored[m.id];
				if (ex.size() != m.cx.size()) {
					ex.assign(m.cx.size(), false);
				}
				g_meshes.push_back(std::move(m));
			}
			++g_version;
		}

		std::string Hex(const std::vector<bool>& a_bits)
		{
			static constexpr char kDigits[] = "0123456789abcdef";
			std::string out;
			out.reserve(a_bits.size() / 4 + 1);
			for (std::size_t i = 0; i < a_bits.size(); i += 4) {
				int v = 0;
				for (std::size_t b = 0; b < 4 && i + b < a_bits.size(); ++b) {
					if (a_bits[i + b]) {
						v |= 1 << b;
					}
				}
				out.push_back(kDigits[v]);
			}
			return out;
		}
	}

	void Reset()
	{
		g_explored.clear();
		g_meshes.clear();
		g_areaKey.clear();
		++g_version;
	}

	void Tick(RE::PlayerCharacter* a_player)
	{
		RefreshArea(a_player);
		const auto pos = a_player->GetPosition();
		const float r2 = kRevealRadius * kRevealRadius;
		for (const auto& m : g_meshes) {
			auto& ex = g_explored[m.id];
			for (std::size_t i = 0; i < m.cx.size() && i < ex.size(); ++i) {
				if (ex[i]) {
					continue;
				}
				const float dx = m.cx[i] - pos.x, dy = m.cy[i] - pos.y;
				if (dx * dx + dy * dy <= r2) {
					ex[i] = true;
					g_exploredChanged = true;
				}
			}
		}
		if (g_exploredChanged) {
			g_exploredChanged = false;
			++g_version;
		}
	}

	std::string Get(std::uint32_t a_haveVersion)
	{
		if (a_haveVersion == g_version) {
			return R"({"same":true})";
		}
		const auto player = RE::PlayerCharacter::GetSingleton();
		if (!player || !player->GetParentCell()) {
			return R"({"ready":false})";
		}
		RefreshArea(player);
		std::string key;
		const auto cells = AreaCells(player, key);

		// Floor plan: one vertex list and one triangle list for the whole area.
		json verts = json::array(), tris = json::array();
		std::vector<bool> allBits;
		std::size_t vbase = 0;
		for (const auto mesh : MeshesIn(cells)) {
			for (const auto& v : mesh->vertices) {
				verts.push_back(static_cast<int>(v.location.x));
				verts.push_back(static_cast<int>(v.location.y));
				verts.push_back(static_cast<int>(v.location.z));
			}
			for (const auto& t : mesh->triangles) {
				tris.push_back(vbase + t.vertices[0]);
				tris.push_back(vbase + t.vertices[1]);
				tris.push_back(vbase + t.vertices[2]);
			}
			vbase += mesh->vertices.size();
			const auto it = g_explored.find(mesh->GetFormID());
			for (std::size_t i = 0; i < mesh->triangles.size(); ++i) {
				allBits.push_back(it != g_explored.end() && i < it->second.size() && it->second[i]);
			}
		}
		const std::string explored = Hex(allBits);

		// Doors, containers and ore veins in the area.
		json doors = json::array(), containers = json::array(), ores = json::array();
		for (const auto cell : cells) {
			cell->ForEachReference([&](RE::TESObjectREFR* a_ref) {
				if (!a_ref || a_ref->IsDisabled() || a_ref->IsDeleted()) {
					return RE::BSContainer::ForEachResult::kContinue;
				}
				const auto base = a_ref->GetBaseObject();
				if (!base) {
					return RE::BSContainer::ForEachResult::kContinue;
				}
				const auto pos = a_ref->GetPosition();
				switch (base->GetFormType()) {
				case RE::FormType::Door:
					if (const auto tele = a_ref->extraList.GetByType<RE::ExtraTeleport>(); tele && tele->teleportData) {
						std::string to;
						if (const auto linked = tele->teleportData->linkedDoor.get(); linked) {
							const auto lc = linked->GetParentCell();
							const auto ll = linked->GetCurrentLocation();
							to = lc && lc->IsInteriorCell() ? Str(lc->GetName()) : ll ? Str(ll->GetName()) : "Outside";
						}
						doors.push_back({ { "x", pos.x }, { "y", pos.y }, { "z", pos.z }, { "to", to } });
					}
					break;
				case RE::FormType::Container:
					containers.push_back({ { "x", pos.x }, { "y", pos.y }, { "z", pos.z } });
					break;
				case RE::FormType::Activator:
					{
						const std::string name = Str(a_ref->GetDisplayFullName());
						if (name.find("Vein") != std::string::npos) {
							const auto ore = extras::OreVein(a_ref);
							ores.push_back({ { "x", pos.x }, { "y", pos.y }, { "z", pos.z }, { "name", name }, { "depleted", ore.is_object() && ore.value("oreLeft", -1) == 0 } });
						}
						break;
					}
				default:
					break;
				}
				return RE::BSContainer::ForEachResult::kContinue;
			});
		}

		const auto cell = player->GetParentCell();
		json out{
			{ "version", g_version },
			{ "interior", cell->IsInteriorCell() },
			{ "name", cell->IsInteriorCell() ? Str(cell->GetName()) : "" },
			{ "verts", verts },
			{ "tris", tris },
			{ "explored", explored },
			{ "doors", doors },
			{ "containers", containers },
			{ "ores", ores }
		};
		return out.dump(-1, ' ', false, json::error_handler_t::replace);
	}
}
