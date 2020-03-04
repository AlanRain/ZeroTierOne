/*
 * Copyright (c)2013-2020 ZeroTier, Inc.
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file in the project's root directory.
 *
 * Change Date: 2024-01-01
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2.0 of the Apache License.
 */
/****/

#ifndef ZT_CAPABILITY_HPP
#define ZT_CAPABILITY_HPP

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "Constants.hpp"
#include "Credential.hpp"
#include "Address.hpp"
#include "C25519.hpp"
#include "Utils.hpp"
#include "Identity.hpp"

#define ZT_VIRTUALNETWORKRULE_MARSHAL_SIZE_MAX 21

#define ZT_CAPABILITY__CUSTODY_CHAIN_ITEM_MARSHAL_SIZE_MAX (5 + 5 + 2 + ZT_SIGNATURE_BUFFER_SIZE)
#define ZT_CAPABILITY_MARSHAL_SIZE_MAX (8 + 8 + 4 + 1 + 2 + (ZT_VIRTUALNETWORKRULE_MARSHAL_SIZE_MAX * ZT_MAX_CAPABILITY_RULES) + 2 + (ZT_CAPABILITY__CUSTODY_CHAIN_ITEM_MARSHAL_SIZE_MAX * ZT_MAX_CAPABILITY_CUSTODY_CHAIN_LENGTH))

namespace ZeroTier {

class RuntimeEnvironment;

/**
 * A set of grouped and signed network flow rules
 *
 * On the sending side the sender does the following for each packet:
 *
 * (1) Evaluates its capabilities in ascending order of ID to determine
 *     which capability allows it to transmit this packet.
 * (2) If it has not done so lately, it then sends this capability to the
 *     receiving peer ("presents" it).
 * (3) The sender then sends the packet.
 *
 * On the receiving side the receiver evaluates the capabilities presented
 * by the sender. If any valid un-expired capability allows this packet it
 * is accepted.
 *
 * Note that this is after evaluation of network scope rules and only if
 * network scope rules do not deliver an explicit match.
 *
 * Capabilities support a chain of custody. This is currently unused but
 * in the future would allow the publication of capabilities that can be
 * handed off between nodes. Limited transferability of capabilities is
 * a feature of true capability based security.
 */
class Capability : public Credential
{
	friend class Credential;

public:
	static constexpr ZT_CredentialType credentialType() noexcept { return ZT_CREDENTIAL_TYPE_CAPABILITY; }

	ZT_INLINE Capability() noexcept { memoryZero(this); }

	/**
	 * @param id Capability ID
	 * @param nwid Network ID
	 * @param ts Timestamp (at controller)
	 * @param mccl Maximum custody chain length (1 to create non-transferable capability)
	 * @param rules Network flow rules for this capability
	 * @param ruleCount Number of flow rules
	 */
	ZT_INLINE Capability(const uint32_t id,const uint64_t nwid,const int64_t ts,const unsigned int mccl,const ZT_VirtualNetworkRule *const rules,const unsigned int ruleCount) noexcept :
		_nwid(nwid),
		_ts(ts),
		_id(id),
		_maxCustodyChainLength((mccl > 0) ? ((mccl < ZT_MAX_CAPABILITY_CUSTODY_CHAIN_LENGTH) ? mccl : (unsigned int)ZT_MAX_CAPABILITY_CUSTODY_CHAIN_LENGTH) : 1),
		_ruleCount((ruleCount < ZT_MAX_CAPABILITY_RULES) ? ruleCount : ZT_MAX_CAPABILITY_RULES)
	{
		if (_ruleCount > 0)
			memcpy(_rules,rules,sizeof(ZT_VirtualNetworkRule) * _ruleCount);
	}

	/**
	 * @return Rules -- see ruleCount() for size of array
	 */
	ZT_INLINE const ZT_VirtualNetworkRule *rules() const noexcept { return _rules; }

	/**
	 * @return Number of rules in rules()
	 */
	ZT_INLINE unsigned int ruleCount() const noexcept { return _ruleCount; }

	/**
	 * @return ID and evaluation order of this capability in network
	 */
	ZT_INLINE uint32_t id() const noexcept { return _id; }

	/**
	 * @return Network ID for which this capability was issued
	 */
	ZT_INLINE uint64_t networkId() const noexcept { return _nwid; }

	/**
	 * @return Timestamp
	 */
	ZT_INLINE int64_t timestamp() const noexcept { return _ts; }

	/**
	 * @return Last 'to' address in chain of custody
	 */
	ZT_INLINE Address issuedTo() const noexcept
	{
		Address i2;
		for(int i=0;i<ZT_MAX_CAPABILITY_CUSTODY_CHAIN_LENGTH;++i) {
			if (!_custody[i].to)
				return i2;
			else i2 = _custody[i].to;
		}
		return i2;
	}

	/**
	 * Sign this capability and add signature to its chain of custody
	 *
	 * If this returns false, this object should be considered to be
	 * in an undefined state and should be discarded. False can be returned
	 * if there is no more room for signatures (max chain length reached)
	 * or if the 'from' identity does not include a secret key to allow
	 * it to sign anything.
	 *
	 * @param from Signing identity (must have secret)
	 * @param to Recipient of this signature
	 * @return True if signature successful and chain of custody appended
	 */
	bool sign(const Identity &from,const Address &to) noexcept;

	/**
	 * Verify this capability's chain of custody and signatures
	 *
	 * @param RR Runtime environment to provide for peer lookup, etc.
	 */
	ZT_INLINE Credential::VerifyResult verify(const RuntimeEnvironment *RR,void *tPtr) const noexcept { return _verify(RR,tPtr,*this); }

	static constexpr int marshalSizeMax() noexcept { return ZT_CAPABILITY_MARSHAL_SIZE_MAX; }
	int marshal(uint8_t data[ZT_CAPABILITY_MARSHAL_SIZE_MAX],bool forSign = false) const noexcept;
	int unmarshal(const uint8_t *data,int len) noexcept;

	/**
	 * Marshal a set of virtual network rules
	 *
	 * @param data Buffer to store rules (must be at least ruleCount * ZT_VIRTUALNETWORKRULE_MARSHAL_SIZE_MAX)
	 * @param rules Network rules
	 * @param ruleCount Number of rules
	 * @return Number of bytes written or -1 on error
	 */
	static int marshalVirtualNetworkRules(uint8_t *data,const ZT_VirtualNetworkRule *rules,unsigned int ruleCount) noexcept;

	/**
	 * Unmarshal a set of virtual network rules
	 *
	 * @param data Rule set to unmarshal
	 * @param len Length of data
	 * @param rules Buffer to store rules
	 * @param ruleCount Result parameter to set to the number of rules decoded
	 * @param maxRuleCount Capacity of rules buffer
	 * @return Number of bytes unmarshaled or -1 on error
	 */
	static int unmarshalVirtualNetworkRules(const uint8_t *data,int len,ZT_VirtualNetworkRule *rules,unsigned int &ruleCount,unsigned int maxRuleCount) noexcept;

	// Provides natural sort order by ID
	ZT_INLINE bool operator<(const Capability &c) const noexcept { return (_id < c._id); }

	ZT_INLINE bool operator==(const Capability &c) const noexcept { return (memcmp(this,&c,sizeof(Capability)) == 0); }
	ZT_INLINE bool operator!=(const Capability &c) const noexcept { return (memcmp(this,&c,sizeof(Capability)) != 0); }

private:
	uint64_t _nwid;
	int64_t _ts;
	uint32_t _id;

	unsigned int _maxCustodyChainLength;

	unsigned int _ruleCount;
	ZT_VirtualNetworkRule _rules[ZT_MAX_CAPABILITY_RULES];

	struct {
		Address to;
		Address from;
		unsigned int signatureLength;
		uint8_t signature[ZT_SIGNATURE_BUFFER_SIZE];
	} _custody[ZT_MAX_CAPABILITY_CUSTODY_CHAIN_LENGTH];
};

} // namespace ZeroTier

#endif
