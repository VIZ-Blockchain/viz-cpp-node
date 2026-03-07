# VIZ CLI Wallet — Complete Command Reference

Complete reference for all `cli_wallet` commands with syntax and examples.

---

## Table of Contents

1. [Wallet Management](#wallet-management)
2. [Key Management](#key-management)
3. [Query Operations](#query-operations)
4. [Account Operations](#account-operations)
5. [Transfer & Vesting](#transfer--vesting)
6. [Witness Operations](#witness-operations)
7. [Content Operations](#content-operations)
8. [Escrow Operations](#escrow-operations)
9. [Recovery Operations](#recovery-operations)
10. [Committee Operations](#committee-operations)
11. [Invite System](#invite-system)
12. [Award Operations](#award-operations)
13. [Subscription Operations](#subscription-operations)
14. [Account Market](#account-market)
15. [Proposal Operations](#proposal-operations)
16. [Transaction Builder](#transaction-builder)
17. [NS DNS Helpers](#ns-dns-helpers)
18. [Private Messaging](#private-messaging)

---

## Wallet Management

### help
Returns a list of all commands supported by the wallet API.

```bash
help
```

### gethelp
Returns detailed help on a single API command.

```bash
gethelp "transfer"
```

### about
Returns info such as client version, git version, version of boost, openssl.

```bash
about
```

### is_new
Checks whether the wallet has just been created and has not yet had a password set.

```bash
is_new
# Returns: true or false
```

### is_locked
Checks whether the wallet is locked (is unable to use its private keys).

```bash
is_locked
# Returns: true or false
```

### lock
Locks the wallet immediately.

```bash
lock
```

### unlock
Unlocks the wallet.

```bash
unlock "your_password"
```

### set_password
Sets a new password on the wallet. The wallet must be either 'new' or 'unlocked'.

```bash
set_password "your_new_password"
```

### load_wallet_file
Loads a specified wallet file.

```bash
load_wallet_file "wallet.json"
# Or reload current file:
load_wallet_file ""
```

### save_wallet_file
Saves the current wallet to the given filename.

```bash
save_wallet_file "backup_wallet.json"
# Or save to current filename:
save_wallet_file ""
```

### quit
Quits the wallet application.

```bash
quit
```

### set_transaction_expiration
Sets the amount of time in the future until a transaction expires.

```bash
set_transaction_expiration 60  # 60 seconds
```

---

## Key Management

### import_key
Imports a WIF Private Key into the wallet to be used to sign transactions.

```bash
import_key "5KQwrPbwdL6PhXujxW37FSSQZ1JiwsST4cqQzDeyXtP79zkvFD3"
```

### suggest_brain_key
Suggests a safe brain key to use for creating your account.

```bash
suggest_brain_key
# Returns: { "brain_priv_key": "...", "pub_key": "VIZ...", "wif_priv_key": "5K..." }
```

### list_keys
Dumps all private keys owned by the wallet in WIF format.

```bash
list_keys
# Returns: { "VIZpubkey...": "5Kprivkey..." }
```

### get_private_key
Get the WIF private key corresponding to a public key. The private key must already be in the wallet.

```bash
get_private_key "VIZ6MRyAjQq8ud7hVNYcfnVPJqcVpscN5So8BhtHuGYqET5GDW5CV"
```

### get_private_key_from_password
Generates a private key from account name, role, and password.

```bash
get_private_key_from_password "myaccount" "active" "mypassword123"
# Returns: ["VIZpubkey...", "5Kprivkey..."]
```

### normalize_brain_key
Transforms a brain key to reduce the chance of errors when re-entering.

```bash
normalize_brain_key "my brain key words here"
```

---

## Query Operations

### info
Returns info about the current state of the blockchain.

```bash
info
```

### database_info
Returns info about database objects.

```bash
database_info
```

### get_block
Returns the information about a block.

```bash
get_block 1000000
```

### get_ops_in_block
Returns sequence of operations in a specified block.

```bash
get_ops_in_block 1000000 false  # all operations
get_ops_in_block 1000000 true   # only virtual operations
```

### get_active_witnesses
Returns the list of witnesses producing blocks in the current round (21 blocks).

```bash
get_active_witnesses
```

### get_account
Returns information about the given account.

```bash
get_account "alice"
```

### list_accounts
Lists all accounts registered in the blockchain.

```bash
list_accounts "" 100     # First 100 accounts
list_accounts "bob" 100  # 100 accounts starting from "bob"
```

### list_my_accounts
Gets the account information for all accounts for which this wallet has a private key.

```bash
list_my_accounts
```

### get_account_history
Returns account operations history in the range [from-limit, from].

```bash
get_account_history "alice" -1 100  # Last 100 operations
get_account_history "alice" 500 100 # Operations 400-500
```

### get_transaction
Returns transaction by ID.

```bash
get_transaction "0123456789abcdef0123456789abcdef01234567"
```

### get_master_history
Returns master authority history for an account.

```bash
get_master_history "alice"
```

### get_withdraw_routes
Returns vesting withdraw routes for an account.

```bash
get_withdraw_routes "alice" "all"       # all routes
get_withdraw_routes "alice" "incoming"  # incoming routes
get_withdraw_routes "alice" "outgoing"  # outgoing routes
```

---

## Account Operations

### create_account
Creates a new account with auto-generated keys (controlled by this wallet).

```bash
create_account "creator" "1.000 VIZ" "10.000000 SHARES" "newaccount" "{}" true
```

| Parameter | Type | Description |
|-----------|------|-------------|
| creator | string | Account creating the new account |
| tokens_fee | asset | Amount of VIZ to pay as fee |
| delegated_vests | asset | Amount of SHARES to delegate |
| new_account_name | string | Name of the new account |
| json_metadata | string | JSON metadata for the account |
| broadcast | bool | Whether to broadcast the transaction |

### create_account_with_keys
Creates a new account with specified keys (for faucets).

```bash
create_account_with_keys "creator" "1.000 VIZ" "10.000000 SHARES" "newaccount" "{}" \
  "VIZmaster..." "VIZactive..." "VIZregular..." "VIZmemo..." true
```

### update_account
Updates the keys of an existing account.

```bash
update_account "myaccount" "{\"profile\":\"test\"}" \
  "VIZmaster..." "VIZactive..." "VIZregular..." "VIZmemo..." true
```

### update_account_auth_key
Updates a key of an authority for an existing account.

```bash
update_account_auth_key "myaccount" "active" "VIZ6newkey..." 1 true
# Set weight to 0 to remove key:
update_account_auth_key "myaccount" "active" "VIZ6oldkey..." 0 true
```

| Parameter | Type | Description |
|-----------|------|-------------|
| account_name | string | Account to update |
| type | enum | Authority type: `master`, `active`, or `regular` |
| key | public_key | Public key to add/remove |
| weight | uint16 | Weight (0 = remove) |
| broadcast | bool | Whether to broadcast |

### update_account_auth_account
Updates an account authority for an existing account.

```bash
update_account_auth_account "myaccount" "active" "guardian" 1 true
# Remove account from authority:
update_account_auth_account "myaccount" "active" "guardian" 0 true
```

### update_account_auth_threshold
Updates the weight threshold of an authority.

```bash
update_account_auth_threshold "myaccount" "active" 2 true
```

### update_account_meta
Updates the account JSON metadata.

```bash
update_account_meta "myaccount" "{\"profile\":{\"name\":\"Alice\"}}" true
```

### update_account_memo_key
Updates the memo key of an account.

```bash
update_account_memo_key "myaccount" "VIZnewmemokey..." true
```

### delegate_vesting_shares
Delegates SHARES from one account to another.

```bash
delegate_vesting_shares "alice" "bob" "100.000000 SHARES" true
# Remove delegation (delegate 0):
delegate_vesting_shares "alice" "bob" "0.000000 SHARES" true
```

---

## Transfer & Vesting

### transfer
Transfer funds from one account to another.

```bash
transfer "alice" "bob" "10.000 VIZ" "payment memo" true
# Encrypted memo (prefix with #):
transfer "alice" "bob" "10.000 VIZ" "#secret message" true
```

### transfer_to_vesting
Transfer VIZ into vesting fund (SHARES).

```bash
transfer_to_vesting "alice" "bob" "100.000 VIZ" true
# Self-vest:
transfer_to_vesting "alice" "alice" "100.000 VIZ" true
```

### withdraw_vesting
Set up a vesting withdraw request (power down).

```bash
withdraw_vesting "alice" "100.000000 SHARES" true
# Cancel withdrawal (withdraw 0):
withdraw_vesting "alice" "0.000000 SHARES" true
```

### set_withdraw_vesting_route
Set up a vesting withdraw route.

```bash
# Route 50% of withdrawals to "bob" as VIZ:
set_withdraw_vesting_route "alice" "bob" 5000 false true
# Route 25% to "charlie" as SHARES:
set_withdraw_vesting_route "alice" "charlie" 2500 true true
```

| Parameter | Type | Description |
|-----------|------|-------------|
| from | string | Account withdrawing |
| to | string | Destination account |
| percent | uint16 | Percent (100 = 1%, 10000 = 100%) |
| auto_vest | bool | true = receive as SHARES, false = receive as VIZ |
| broadcast | bool | Whether to broadcast |

---

## Witness Operations

### list_witnesses
Lists all witnesses registered in the blockchain.

```bash
list_witnesses "" 100     # First 100 witnesses
list_witnesses "bob" 100  # 100 witnesses starting from "bob"
```

### get_witness
Returns information about the given witness.

```bash
get_witness "witnessname"
```

### update_witness
Update a witness object.

```bash
update_witness "mywitness" "https://mywitness.com" "VIZsigningkey..." true
# Disable block production (empty key):
update_witness "mywitness" "" "" true
```

### update_chain_properties
Vote for the chain properties.

```bash
update_chain_properties "mywitness" \
  {"account_creation_fee":"1.000 VIZ","maximum_block_size":65536,"create_account_delegation_ratio":10,...} \
  true
```

### versioned_update_chain_properties
Vote for the versioned chain properties.

```bash
versioned_update_chain_properties "mywitness" \
  {"account_creation_fee":"1.000 VIZ","maximum_block_size":65536,...} \
  true
```

### set_voting_proxy
Set the voting proxy for an account.

```bash
set_voting_proxy "alice" "trustedvoter" true
# Remove proxy:
set_voting_proxy "alice" "" true
```

### vote_for_witness
Vote for a witness to become a block producer.

```bash
vote_for_witness "alice" "mywitness" true true   # Vote for
vote_for_witness "alice" "mywitness" false true  # Vote against
```

---

## Content Operations

> Note: Content operations are deprecated in VIZ.

### post_content
Post or update a content.

```bash
post_content "author" "my-permlink" "" "" "Title" "Body content" 5000 "{}" true
```

| Parameter | Type | Description |
|-----------|------|-------------|
| author | string | Account authoring the content |
| permlink | string | Unique permlink for the content |
| parent_author | string | Parent author (empty for top-level) |
| parent_permlink | string | Parent permlink (empty for top-level) |
| title | string | Title of the content |
| body | string | Body of the content |
| curation_percent | int16 | Curation reward percent (0-10000) |
| json | string | JSON metadata |
| broadcast | bool | Whether to broadcast |

### vote
Vote on a content.

```bash
vote "voter" "author" "permlink" 100 true   # 100% upvote
vote "voter" "author" "permlink" -100 true  # 100% downvote
vote "voter" "author" "permlink" 0 true     # Remove vote
```

### delete_content
Delete a content.

```bash
delete_content "author" "permlink" true
```

### custom
Broadcast a custom operation.

```bash
custom ["alice"] [] "follow" "{\"follower\":\"alice\",\"following\":\"bob\"}" true
```

| Parameter | Type | Description |
|-----------|------|-------------|
| required_active_auths | array | Accounts requiring active authority |
| required_regular_auths | array | Accounts requiring regular authority |
| id | string | Custom operation type identifier |
| json | string | JSON data for the custom operation |
| broadcast | bool | Whether to broadcast |

---

## Escrow Operations

### escrow_transfer
Transfer funds using escrow.

```bash
escrow_transfer "alice" "bob" "agent" 1 "100.000 VIZ" "1.000 VIZ" \
  "2024-01-15T12:00:00" "2024-02-15T12:00:00" "{}" true
```

| Parameter | Type | Description |
|-----------|------|-------------|
| from | string | Funding account |
| to | string | Destination account |
| agent | string | Escrow agent account |
| escrow_id | uint32 | Unique escrow ID |
| token_amount | asset | Amount to escrow |
| fee | asset | Agent fee |
| ratification_deadline | time | Deadline for approval |
| escrow_expiration | time | Expiration time |
| json_metadata | string | JSON metadata |
| broadcast | bool | Whether to broadcast |

### escrow_approve
Approve a proposed escrow transfer.

```bash
escrow_approve "alice" "bob" "agent" "bob" 1 true true     # Approve
escrow_approve "alice" "bob" "agent" "agent" 1 true true   # Agent approves
escrow_approve "alice" "bob" "agent" "bob" 1 false true    # Reject (refund)
```

### escrow_dispute
Raise a dispute on the escrow transfer.

```bash
escrow_dispute "alice" "bob" "agent" "alice" 1 true  # from disputes
escrow_dispute "alice" "bob" "agent" "bob" 1 true    # to disputes
```

### escrow_release
Release funds held in escrow.

```bash
# Agent releases to bob:
escrow_release "alice" "bob" "agent" "agent" "bob" 1 "100.000 VIZ" true
# After expiration, alice can release to herself:
escrow_release "alice" "bob" "agent" "alice" "alice" 1 "100.000 VIZ" true
```

---

## Recovery Operations

### request_account_recovery
Create an account recovery request as a recovery account.

```bash
request_account_recovery "recovery_account" "account_to_recover" \
  {"weight_threshold":1,"account_auths":[],"key_auths":[["VIZnewkey...",1]]} true
```

### recover_account
Recover your account using a recovery request.

```bash
recover_account "myaccount" \
  {"weight_threshold":1,"account_auths":[],"key_auths":[["VIZoldkey...",1]]} \
  {"weight_threshold":1,"account_auths":[],"key_auths":[["VIZnewkey...",1]]} true
```

### change_recovery_account
Change your recovery account (30 day delay).

```bash
change_recovery_account "myaccount" "new_recovery_account" true
```

---

## Committee Operations

### committee_worker_create_request
Create a committee worker request.

```bash
committee_worker_create_request "creator" "https://proposal.com/info" "worker" \
  "100.000 VIZ" "500.000 VIZ" 2592000 true
```

| Parameter | Type | Description |
|-----------|------|-------------|
| creator | string | Account creating the request |
| url | string | URL with request information |
| worker | string | Worker account to receive payment |
| required_amount_min | asset | Minimum amount requested |
| required_amount_max | asset | Maximum amount requested |
| duration | uint32 | Duration in seconds |
| broadcast | bool | Whether to broadcast |

### committee_worker_cancel_request
Cancel a committee worker request.

```bash
committee_worker_cancel_request "creator" 123 true
```

### committee_vote_request
Vote on a committee worker request.

```bash
committee_vote_request "voter" 123 10000 true   # 100% support
committee_vote_request "voter" 123 -10000 true  # 100% against
committee_vote_request "voter" 123 0 true       # Remove vote
```

---

## Invite System

### create_invite
Create an invite with a balance.

```bash
create_invite "creator" "10.000 VIZ" "VIZinvitekey..." true
```

### claim_invite_balance
Claim an invite balance to an existing account.

```bash
claim_invite_balance "initiator" "receiver" "5Kinvitesecret..." true
```

### invite_registration
Register a new account using an invite.

```bash
invite_registration "initiator" "newaccount" "5Kinvitesecret..." "VIZnewaccountkey..." true
```

### use_invite_balance
Use invite balance to transfer to vesting.

```bash
use_invite_balance "initiator" "receiver" "5Kinvitesecret..." true
```

---

## Award Operations

### award
Award an account (energy-based reward).

```bash
# Simple award:
award "initiator" "receiver" 1000 0 "Great work!" [] true

# Award with beneficiaries:
award "initiator" "receiver" 1000 0 "memo" \
  [{"account":"beneficiary1","weight":5000},{"account":"beneficiary2","weight":5000}] true
```

| Parameter | Type | Description |
|-----------|------|-------------|
| initiator | string | Account giving the award |
| receiver | string | Account receiving the award |
| energy | uint16 | Energy to use (0-10000 = 0-100%) |
| custom_sequence | uint64 | Custom sequence number |
| memo | string | Memo for the award |
| beneficiaries | array | List of beneficiaries with weights |
| broadcast | bool | Whether to broadcast |

### fixed_award
Fixed award an account (fixed amount reward).

```bash
fixed_award "initiator" "receiver" "10.000 VIZ" 10000 0 "Fixed award" [] true
```

| Parameter | Type | Description |
|-----------|------|-------------|
| initiator | string | Account giving the award |
| receiver | string | Account receiving the award |
| reward_amount | asset | Fixed reward amount |
| max_energy | uint16 | Maximum energy to use |
| custom_sequence | uint64 | Custom sequence number |
| memo | string | Memo for the award |
| beneficiaries | array | List of beneficiaries |
| broadcast | bool | Whether to broadcast |

---

## Subscription Operations

### set_paid_subscription
Set up a paid subscription.

```bash
set_paid_subscription "creator" "https://sub.com/info" 5 "10.000 VIZ" 2592000 true
```

| Parameter | Type | Description |
|-----------|------|-------------|
| account | string | Account setting up subscription |
| url | string | URL with subscription info |
| levels | uint16 | Number of subscription levels |
| amount | asset | Cost per level |
| period | uint16 | Subscription period in seconds |
| broadcast | bool | Whether to broadcast |

### paid_subscribe
Subscribe to a paid subscription.

```bash
paid_subscribe "subscriber" "creator" 1 "10.000 VIZ" 2592000 true true
```

| Parameter | Type | Description |
|-----------|------|-------------|
| subscriber | string | Subscribing account |
| account | string | Account with the subscription |
| level | uint16 | Subscription level |
| amount | asset | Payment amount |
| period | uint16 | Subscription period |
| auto_renewal | bool | Enable auto renewal |
| broadcast | bool | Whether to broadcast |

---

## Account Market

### set_account_price
Set an account up for sale.

```bash
# Put account on sale:
set_account_price "myaccount" "myaccount" "100.000 VIZ" true true
# Remove from sale:
set_account_price "myaccount" "myaccount" "0.000 VIZ" false true
```

### set_subaccount_price
Set subaccount creation for sale.

```bash
# Allow subaccount creation for a fee:
set_subaccount_price "myaccount" "myaccount" "50.000 VIZ" true true
# Disable:
set_subaccount_price "myaccount" "myaccount" "0.000 VIZ" false true
```

### buy_account
Buy an account.

```bash
buy_account "buyer" "accountforsale" "100.000 VIZ" "VIZnewkey..." "0.000 VIZ" true
```

| Parameter | Type | Description |
|-----------|------|-------------|
| buyer | string | Buying account |
| account | string | Account being bought |
| account_offer_price | asset | Purchase price |
| account_authorities_key | public_key | New public key for the account |
| tokens_to_shares | asset | Amount to convert to shares |
| broadcast | bool | Whether to broadcast |

### target_account_sale
Set an account for sale to a specific target buyer.

```bash
target_account_sale "myaccount" "myaccount" "targetbuyer" "100.000 VIZ" true true
```

---

## Proposal Operations

### approve_proposal
Approve or disapprove a proposal.

```bash
# Add active approval:
approve_proposal "author" "proposal_title" \
  {"active_approvals_to_add":["myaccount"],"active_approvals_to_remove":[]} true

# Remove master approval:
approve_proposal "author" "proposal_title" \
  {"master_approvals_to_remove":["myaccount"]} true
```

### delete_proposal
Delete a proposal.

```bash
delete_proposal "author" "proposal_title" "requester" true
```

### get_proposed_transactions
Returns proposals for the account.

```bash
get_proposed_transactions "myaccount" 0 100
```

---

## Transaction Builder

### begin_builder_transaction
Begins building a new transaction.

```bash
begin_builder_transaction
# Returns: transaction handle (e.g., 0)
```

### add_operation_to_builder_transaction
Adds an operation to a builder transaction.

```bash
add_operation_to_builder_transaction 0 [2,{"from":"alice","to":"bob","amount":"10.000 VIZ","memo":""}]
```

### add_operation_copy_to_builder_transaction
Copies an operation from one builder transaction to another.

```bash
add_operation_copy_to_builder_transaction 0 1 0
```

### replace_operation_in_builder_transaction
Replaces an operation in a builder transaction.

```bash
replace_operation_in_builder_transaction 0 0 [2,{"from":"alice","to":"charlie","amount":"5.000 VIZ","memo":""}]
```

### preview_builder_transaction
Previews a builder transaction.

```bash
preview_builder_transaction 0
```

### sign_builder_transaction
Signs and optionally broadcasts a builder transaction.

```bash
sign_builder_transaction 0 true   # Sign and broadcast
sign_builder_transaction 0 false  # Sign only
```

### propose_builder_transaction
Creates a proposal from a builder transaction.

```bash
propose_builder_transaction 0 "author" "proposal_title" "memo" \
  "2024-02-01T00:00:00" "2024-01-15T00:00:00" true
```

### remove_builder_transaction
Removes a builder transaction.

```bash
remove_builder_transaction 0
```

### get_prototype_operation
Returns an uninitialized object representing a given blockchain operation.

```bash
get_prototype_operation "transfer_operation"
get_prototype_operation "award_operation"
```

### serialize_transaction
Converts a signed transaction in JSON form to its binary representation.

```bash
serialize_transaction {"ref_block_num":...,"operations":[...]}
```

### sign_transaction
Signs a transaction with the necessary keys.

```bash
sign_transaction {"ref_block_num":...,"operations":[...]} true
```

---

## NS DNS Helpers

VIZ DNS Nameserver helpers for storing DNS records in account metadata.

### ns_validate_ipv4
Validates an IPv4 address string.

```bash
ns_validate_ipv4 "188.120.231.153"
# Returns: true

ns_validate_ipv4 "256.0.0.1"
# Returns: false
```

### ns_validate_sha256_hash
Validates a SHA256 hash string (64 hex characters).

```bash
ns_validate_sha256_hash "4a4613daef37cbc5c4a5156cd7b24ea2e6ee2e5f1e7461262a2df2b63cbf17e2"
# Returns: true
```

### ns_validate_ttl
Validates a TTL value (must be positive integer).

```bash
ns_validate_ttl 28800
# Returns: true
```

### ns_validate_ssl_txt_record
Validates an SSL TXT record format (ssl=<hash>).

```bash
ns_validate_ssl_txt_record "ssl=4a4613daef37cbc5c4a5156cd7b24ea2e6ee2e5f1e7461262a2df2b63cbf17e2"
# Returns: true
```

### ns_validate_metadata
Performs complete validation of NS metadata options.

```bash
ns_validate_metadata {"a_records":["188.120.231.153"],"ssl_hash":"4a4613...","ttl":28800}
# Returns: {"is_valid":true,"errors":[]}
```

### ns_create_metadata
Creates NS metadata JSON object from options.

```bash
ns_create_metadata {"a_records":["188.120.231.153"],"ssl_hash":"4a4613daef37cbc5c4a5156cd7b24ea2e6ee2e5f1e7461262a2df2b63cbf17e2","ttl":28800}
# Returns: {"ns":[["A","188.120.231.153"],["TXT","ssl=4a4613..."]],"ttl":28800}
```

### ns_get_summary
Gets complete NS summary from an account's metadata.

```bash
ns_get_summary "myaccount"
# Returns: {"a_records":["188.120.231.153"],"ssl_hash":"4a4613...","ttl":28800,"has_ns_data":true}
```

### ns_extract_a_records
Extracts A records (IPv4 addresses) from an account's metadata.

```bash
ns_extract_a_records "myaccount"
# Returns: ["188.120.231.153", "192.168.1.100"]
```

### ns_extract_ssl_hash
Extracts SSL hash from an account's metadata TXT records.

```bash
ns_extract_ssl_hash "myaccount"
# Returns: "4a4613daef37cbc5c4a5156cd7b24ea2e6ee2e5f1e7461262a2df2b63cbf17e2"
```

### ns_extract_ttl
Extracts TTL value from an account's metadata.

```bash
ns_extract_ttl "myaccount"
# Returns: 28800
```

### ns_set_records
Sets NS records for an account (merges with existing metadata).

```bash
# Set A record only:
ns_set_records "myaccount" {"a_records":["188.120.231.153"],"ttl":28800} true

# Set A records with SSL hash:
ns_set_records "myaccount" \
  {"a_records":["188.120.231.153","192.168.1.100"],"ssl_hash":"4a4613daef37cbc5c4a5156cd7b24ea2e6ee2e5f1e7461262a2df2b63cbf17e2","ttl":28800} \
  true

# Round-robin DNS with multiple A records:
ns_set_records "myaccount" \
  {"a_records":["188.120.231.153","192.168.1.100","10.0.0.50"],"ttl":3600} \
  true
```

| Parameter | Type | Description |
|-----------|------|-------------|
| account_name | string | Account to update |
| options | object | NS metadata options |
| options.a_records | array | List of IPv4 addresses |
| options.ssl_hash | string | SHA256 hash for SSL (optional) |
| options.ttl | uint32 | TTL in seconds (default: 28800) |
| broadcast | bool | Whether to broadcast |

### ns_remove_records
Removes NS records from an account's metadata (preserves other fields).

```bash
ns_remove_records "myaccount" true
```

---

## Private Messaging

### get_encrypted_memo
Returns the encrypted memo if memo starts with '#', otherwise returns memo.

```bash
get_encrypted_memo "alice" "bob" "#secret message"
```

### decrypt_memo
Returns the decrypted memo if possible given wallet's known private keys.

```bash
decrypt_memo "#encrypteddata..."
```

### get_inbox
Gets private messages received by an account.

```bash
get_inbox "myaccount" "2024-01-15T00:00:00" 100 0
```

### get_outbox
Gets private messages sent by an account.

```bash
get_outbox "myaccount" "2024-01-15T00:00:00" 100 0
```

---

## Data Types Reference

### Asset Format
- **VIZ**: 3 decimal places, e.g., `"10.000 VIZ"`
- **SHARES**: 6 decimal places, e.g., `"10.000000 SHARES"`

### Authority Object
```json
{
  "weight_threshold": 1,
  "account_auths": [["guardian", 1]],
  "key_auths": [["VIZ6...", 1]]
}
```

### Beneficiary Route
```json
{"account": "beneficiary", "weight": 5000}
```
Weight is in basis points (5000 = 50%).

### NS Metadata Options
```json
{
  "a_records": ["188.120.231.153"],
  "ssl_hash": "4a4613daef37cbc5c4a5156cd7b24ea2e6ee2e5f1e7461262a2df2b63cbf17e2",
  "ttl": 28800
}
```

---

## Authority Requirements Summary

| Operation | Required Authority |
|-----------|-------------------|
| Account creation | active |
| Account update | master (for keys), active (for other) |
| Account metadata | regular |
| Transfer VIZ | active |
| Transfer SHARES | master |
| Vesting operations | active |
| Witness operations | active |
| Content operations | regular |
| Custom operation | active or regular (specified) |
| Recovery operations | varies |
| Committee operations | regular |
| Invite operations | active |
| Award operations | regular |
| Subscription operations | active |
| Account sale | master |
| NS operations | regular (via account_metadata) |
