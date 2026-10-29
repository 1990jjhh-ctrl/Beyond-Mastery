#pragma once

// The Papyrus natives of the BeyondMastery script (papyrus/BeyondMastery.psc),
// which the MCM uses to read and change the custom skill settings.

namespace BM::Papyrus
{
	// Registers every native; the callback for SKSE's Papyrus interface.
	bool Register(RE::BSScript::IVirtualMachine* a_vm);
}
