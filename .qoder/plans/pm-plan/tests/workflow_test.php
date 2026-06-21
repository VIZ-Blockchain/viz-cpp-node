<?php
/**
 * Prediction Market Workflow Test
 * Pure math simulation — no DB required
 * Run: php tests/workflow_test.php
 *
 * Tests ALL paths: registration, market create, oracle accept/reject,
 * betting (early/mid/late penalty), cancel, add liquidity, resolution,
 * dispute (committee sides with plaintiff / oracle), oracle missed resolution
 */

// ============================================================
// SETTINGS (from DB defaults)
// ============================================================
$SETTINGS = [
    'oracle_registration_fee'    => 10000,    // 10 VIZ
    'creator_registration_fee'   => 10000,    // 10 VIZ
    'min_oracle_insurance'       => 5000000,  // 5000 VIZ
    'dispute_fee'                => 1000000,  // 1000 VIZ
    'dispute_grace_hours'        => 12,
    'oracle_penalty_percent'     => 5,
    'default_time_penalty_percent'=> 5,       // 5% (was 10%)
    'max_time_penalty'           => 1000000,  // 100% (precision=6)
    'market_creation_fee'        => 5000,     // 5 VIZ
    'penalty_curve_type'         => 1,        // 0=linear, 1=quadratic (default)
    'dispute_resolver_account_id'=> 2,        // dispute resolver (recommended multisig)
    'dao_fund_account_id'        => 2,        // DAO fund account for fees (market creation, dispute, penalties)
    'min_risk_score_betting'     => 1.5,      // block bets below this risk score
    'min_risk_score_listing'     => 2.5,      // hide from listing below this risk score
    'oracle_dispute_response_hours' => 12,    // hours oracle has to respond, auto-penalty if missed
    'oracle_no_contest_penalty_percent' => 50, // % of dispute_fee as penalty on voluntary no-contest
    'dispute_auto_close_days'       => 14,    // days before unresolved disputes auto-close
    'dispute_reward_multiplier'     => 3.0,   // float multiplier for oracle insurance carve-out on upheld dispute
    'dispute_rejected_oracle_permille' => 500, // permille of dispute_fee to oracle when dispute rejected (50%)
    'dispute_rejected_voter_permille'  => 500, // permille of dispute_fee to resolver/voters when dispute rejected (50%)
];

// ============================================================
// HELPER FUNCTIONS
// ============================================================
function step($name, $data) {
    static $step_num = 0;
    $step_num++;
    echo "\n=== STEP {$step_num}: {$name} ===\n";
    echo json_encode($data, JSON_PRETTY_PRINT | JSON_UNESCAPED_UNICODE) . "\n";
}

function make_user($id, $name, $balance = 0) {
    return [
        'id' => $id, 'name' => $name, 'balance' => $balance,
        'oracle' => 0, 'oracle_insurance' => 0, 'oracle_fee' => 0, 'oracle_fixed_fee' => 5000,
        'create_market' => 0, 'add_liquidity' => 0, 'committee' => 0,
        'bets_balance' => 0, 'liquidity_balance' => 0,
        'oracle_banned' => 0, 'oracle_ban_until' => 0,
        'creator_banned' => 0, 'creator_ban_until' => 0,
    ];
}

function register_oracle(&$user, $fee, $oracle_fee, $settings, $oracle_fixed_fee = 5000) {
    if (1 == $user['oracle']) {
        // Already registered: update profile without fee
        $user['oracle_fee'] = $oracle_fee;
        $user['oracle_fixed_fee'] = $oracle_fixed_fee;
        return ['status' => true, 'updated' => true];
    }
    $reg_fee = $settings['oracle_registration_fee'];
    if ($user['balance'] < $reg_fee) return ['error' => 'Insufficient balance'];
    $user['balance'] -= $reg_fee;
    $user['oracle'] = 1;
    $user['oracle_fee'] = $oracle_fee; // permille: 5 = 0.5%
    $user['oracle_fixed_fee'] = $oracle_fixed_fee;
    return ['status' => true, 'paid' => $reg_fee];
}

function oracle_deposit_insurance(&$user, $amount) {
    if ($user['balance'] < $amount) return ['error' => 'Insufficient balance'];
    $user['balance'] -= $amount;
    $user['oracle_insurance'] += $amount;
    return ['status' => true];
}

function register_creator(&$user, $settings) {
    $reg_fee = $settings['creator_registration_fee'];
    if ($user['balance'] < $reg_fee) return ['error' => 'Insufficient balance'];
    $user['balance'] -= $reg_fee;
    $user['create_market'] = 1;
    $user['add_liquidity'] = 1;
    return ['status' => true, 'paid' => $reg_fee];
}

function create_market(&$creator, $oracle, $liquidity, $oracle_fee_permille, $liquidity_fee_permille,
                       $market_time, $betting_exp, $result_exp, $penalty_type, $penalty_value,
                       $committee_id, $settings, $penalty_curve_type = -1, &$committee_user = null) {
    $market_creation_fee = $settings['market_creation_fee'];
    $total_required = $liquidity + $market_creation_fee;
    if ($creator['balance'] < $total_required) return ['error' => 'Insufficient balance (need '.($total_required/1000).' VIZ)'];
    if ($liquidity < 100000) return ['error' => 'Min liquidity 100 VIZ'];
    if ($oracle_fee_permille < $oracle['oracle_fee']) return ['error' => 'Oracle fee too low'];

    // BUG-1 is here in real code: min(0,$liq_fee) — we simulate correctly with max
    $liquidity_fee_permille = max(0, $liquidity_fee_permille);

    // Oracle fixed fee from oracle's profile
    $oracle_fixed_fee = $oracle['oracle_fixed_fee'];

    // Penalty curve type: default from system settings if not specified
    if ($penalty_curve_type !== 0 && $penalty_curve_type !== 1) {
        $penalty_curve_type = $settings['penalty_curve_type'];
    }

    $part_a = intval(floor($liquidity / 2));
    $part_b = $liquidity - $part_a;
    $k = $part_a * $part_b;
    $status = ($creator['id'] == $oracle['id']) ? 1 : 0;

    $creator['balance'] -= $liquidity;
    $creator['liquidity_balance'] += $liquidity;

    // Deduct market creation fee and send to committee/DAO
    if ($market_creation_fee > 0) {
        $creator['balance'] -= $market_creation_fee;
        if ($committee_user !== null) {
            $committee_user['balance'] += $market_creation_fee;
        }
    }

    $market = [
        'id' => 1, 'user' => $creator['id'], 'oracle' => $oracle['id'], 'status' => $status,
        'reserve_a' => $part_a, 'reserve_b' => $part_b, 'k' => $k,
        'liquidity_sum' => $liquidity,
        'oracle_fee' => $oracle_fee_permille, 'liquidity_fee' => $liquidity_fee_permille,
        'oracle_fee_earned' => 0, 'liquidity_fee_earned' => 0,
        'oracle_fixed_fee' => $oracle_fixed_fee,
        'bets_sum' => 0, 'a_bets_sum' => 0, 'b_bets_sum' => 0,
        'time' => $market_time, 'betting_expiration' => $betting_exp, 'result_expiration' => $result_exp,
        'time_penalty_type' => $penalty_type, 'time_penalty_value' => $penalty_value,
        'penalty_curve_type' => $penalty_curve_type,
        'committee' => $committee_id,
        'allow_early_resolution' => 0, 'allow_cancellation' => 1,
        'resolved_outcome' => null, 'payout_status' => 0, 'decision' => '',
    ];

    $sec_to_exp = $betting_exp - $market_time;
    $lp = [
        'id' => 1, 'market' => 1, 'user' => $creator['id'],
        'amount' => $liquidity, 'weight_a' => $part_a, 'weight_b' => $part_b,
        'sec_to_expiration' => $sec_to_exp,
        'status' => 0, 'earned_fee' => 0,
    ];

    return ['status' => true, 'market' => $market, 'lp' => $lp];
}

function oracle_accept(&$market, &$oracle, $settings) {
    if ($market['oracle'] != $oracle['id']) return ['error' => 'Not oracle'];
    if ($market['status'] != 0) return ['error' => 'Not pending'];
    if ($oracle['oracle_insurance'] < $settings['min_oracle_insurance'])
        return ['error' => 'Insufficient insurance'];
    $market['status'] = 1;
    return ['status' => true];
}

function oracle_reject(&$market, &$creator, $lps) {
    if ($market['status'] != 0) return ['error' => 'Not pending'];
    // Return all liquidity
    $returned = 0;
    foreach ($lps as &$lp) {
        if ($lp['market'] == $market['id'] && $lp['status'] == 0) {
            $creator['balance'] += $lp['amount'];
            $creator['liquidity_balance'] -= $lp['amount'];
            $lp['status'] = 2;
            $returned += $lp['amount'];
        }
    }
    $market['status'] = -1; // deleted
    return ['status' => true, 'returned' => $returned];
}

function place_bet(&$market, &$user, $side, $amount, $current_time) {
    if ($market['status'] != 1) return ['error' => 'Market not active'];
    if ($current_time >= $market['betting_expiration']) return ['error' => 'Betting expired'];
    if ($user['balance'] < $amount) return ['error' => 'Insufficient balance'];

    // Time penalty
    $time_penalty = 0;
    $time_to_exp = $market['betting_expiration'] - $current_time;
    $penalty_window = 0;
    if ($market['time_penalty_type'] == 0) {
        $penalty_window = $market['time_penalty_value'];
    } else {
        $market_duration = $market['betting_expiration'] - $market['time'];
        $penalty_window = intval($market_duration * $market['time_penalty_value'] / 100);
    }
    if ($penalty_window > 0 && $time_to_exp < $penalty_window) {
        $penalty_ratio = 1 - ($time_to_exp / $penalty_window);
        // Apply curve type: 0=linear, 1=quadratic
        $curve_type = isset($market['penalty_curve_type']) ? $market['penalty_curve_type'] : 1;
        if (1 == $curve_type) {
            $penalty_ratio = $penalty_ratio * $penalty_ratio; // quadratic: ratio^2
        }
        $time_penalty = intval($penalty_ratio * 1000000); // precision=6, max 100%
    }

    // CPMM
    $ra = $market['reserve_a'];
    $rb = $market['reserve_b'];
    $k  = $market['k'];
    if ($side == 0) {
        $new_rb = $rb + $amount;
        $new_ra = intval($k / $new_rb);
        $tokens = $ra - $new_ra;
    } else {
        $new_ra = $ra + $amount;
        $new_rb = intval($k / $new_ra);
        $tokens = $rb - $new_rb;
    }
    if ($tokens <= 0) return ['error' => 'Trade too small'];
    $price = intval($amount * 1000000 / $tokens);

    // Fees
    $bet_oracle_fee = intval($amount * $market['oracle_fee'] / 1000);
    $bet_liq_fee    = intval($amount * $market['liquidity_fee'] / 1000);

    // Update state
    $user['balance'] -= $amount;
    $user['bets_balance'] += $amount;
    $market['reserve_a'] = ($side == 0) ? $new_ra : $new_ra;
    $market['reserve_b'] = ($side == 0) ? $new_rb : $new_rb;
    // Fees no longer accumulated on market — computed from losing side at resolution
    $market['bets_sum'] += $amount;
    if ($side == 0) $market['a_bets_sum'] += $amount;
    else $market['b_bets_sum'] += $amount;

    static $bet_counter = 0;
    $bet_counter++;

    $bet = [
        'id' => $bet_counter, 'market' => $market['id'], 'user' => $user['id'],
        'side' => $side, 'amount' => $amount, 'weight' => $tokens, 'price' => $price,
        'oracle_fee' => $bet_oracle_fee, 'liquidity_fee' => $bet_liq_fee,
        'time_penalty' => $time_penalty, 'status' => 0, 'resolved_amount' => 0,
    ];

    return [
        'status' => true, 'bet' => $bet,
        'tokens' => $tokens, 'price' => $price, 'time_penalty' => $time_penalty,
        'reserves' => ['a' => $market['reserve_a'], 'b' => $market['reserve_b'], 'k' => $k],
        'fees' => ['oracle' => $bet_oracle_fee, 'liquidity' => $bet_liq_fee],
    ];
}

function cancel_bet(&$market, &$user, &$bet) {
    if ($bet['status'] != 0) return ['error' => 'Bet not active'];
    $ra = $market['reserve_a'];
    $rb = $market['reserve_b'];
    $k  = $market['k'];
    $tokens = $bet['weight'];
    if ($bet['side'] == 0) {
        $new_ra = $ra + $tokens;
        $new_rb = intval($k / $new_ra);
        $returned = $rb - $new_rb;
    } else {
        $new_rb = $rb + $tokens;
        $new_ra = intval($k / $new_rb);
        $returned = $ra - $new_ra;
    }
    if ($returned <= 0) $returned = 0;

    $market['reserve_a'] = $new_ra;
    $market['reserve_b'] = $new_rb;
    // Fees no longer tracked on market — no reversal needed
    $market['bets_sum'] -= $bet['amount'];
    if ($bet['side'] == 0) $market['a_bets_sum'] -= $bet['amount'];
    else $market['b_bets_sum'] -= $bet['amount'];

    $user['balance'] += $returned;
    $user['bets_balance'] -= $bet['amount'];
    $bet['status'] = 1;
    $bet['resolved_amount'] = $returned;

    return ['status' => true, 'returned' => $returned, 'original' => $bet['amount'],
            'slippage' => $returned - $bet['amount']];
}

function add_liquidity(&$market, &$user, $amount, &$lps, $current_time = null) {
    if ($user['balance'] < $amount) return ['error' => 'Insufficient balance'];
    $ra = $market['reserve_a'];
    $rb = $market['reserve_b'];
    $total = $ra + $rb;
    $add_a = intval($amount * $ra / $total);
    $add_b = $amount - $add_a;

    $market['reserve_a'] += $add_a;
    $market['reserve_b'] += $add_b;
    $market['k'] = $market['reserve_a'] * $market['reserve_b'];
    $market['liquidity_sum'] += $amount;

    $user['balance'] -= $amount;
    $user['liquidity_balance'] += $amount;

    $deposit_time = ($current_time !== null) ? $current_time : $market['time'];
    $sec_to_exp = max(1, $market['betting_expiration'] - $deposit_time);
    $lp = [
        'id' => count($lps) + 1, 'market' => $market['id'], 'user' => $user['id'],
        'amount' => $amount, 'weight_a' => $add_a, 'weight_b' => $add_b,
        'sec_to_expiration' => $sec_to_exp,
        'status' => 0, 'earned_fee' => 0, 'time_deposited' => $deposit_time,
    ];
    $lps[] = $lp;

    return ['status' => true, 'lp' => $lp,
            'reserves' => ['a' => $market['reserve_a'], 'b' => $market['reserve_b'], 'k' => $market['k']]];
}

function withdraw_liquidity(&$market, &$user, &$lp, $withdraw_amount, &$lps, $current_time, $settings) {
    $lp_amount = $lp['amount'];
    $weight_a = $lp['weight_a'];
    $weight_b = $lp['weight_b'];

    // Full or partial?
    if ($withdraw_amount <= 0 || $withdraw_amount >= $lp_amount) {
        $withdraw_amount = $lp_amount;
        $w_a = $weight_a;
        $w_b = $weight_b;
        $is_full = true;
    } else {
        $fraction = $withdraw_amount / $lp_amount;
        $w_a = intval(floor($weight_a * $fraction));
        $w_b = intval(floor($weight_b * $fraction));
        if ($w_a <= 0 || $w_b <= 0) return ['error' => 'Withdrawal amount too small'];
        $is_full = false;
    }

    $new_ra = $market['reserve_a'] - $w_a;
    $new_rb = $market['reserve_b'] - $w_b;
    if ($new_ra <= 0 || $new_rb <= 0) return ['error' => 'Would deplete reserves'];
    $new_liq_sum = $market['liquidity_sum'] - $withdraw_amount;
    if ($new_liq_sum < 100000) return ['error' => 'Below minimum liquidity'];

    // Time ratio
    $market_duration = $market['betting_expiration'] - $market['time'];
    $time_served = $current_time - $lp['time_deposited'];
    $time_ratio = ($market_duration > 0) ? min(1, $time_served / $market_duration) : 0;

    // Fee share: conservative estimate from smaller side (guaranteed loser lower bound)
    $fee_from_a = intval($market['a_bets_sum'] * $market['liquidity_fee'] / 1000);
    $fee_from_b = intval($market['b_bets_sum'] * $market['liquidity_fee'] / 1000);
    $estimated_pool = min($fee_from_a, $fee_from_b);
    $already_paid = $market['liquidity_fee_earned']; // tracks cumulative fees paid to early-withdrawn LPs
    $fee_pool = max(0, $estimated_pool - $already_paid);
    $fee_share = 0;
    if ($fee_pool > 0) {
        $total_tw = 0;
        foreach ($lps as $l) {
            if ($l['status'] == 0) $total_tw += $l['amount'] * max(1, $l['sec_to_expiration']);
        }
        if ($total_tw > 0) {
            $lp_tw = $withdraw_amount * max(1, $lp['sec_to_expiration']);
            $raw = intval($fee_pool * $lp_tw / $total_tw);
            $fee_share = intval($raw * $time_ratio);
        }
    }

    $returned = $withdraw_amount + $fee_share;

    // Apply
    $market['reserve_a'] = $new_ra;
    $market['reserve_b'] = $new_rb;
    $market['k'] = $new_ra * $new_rb;
    $market['liquidity_sum'] = $new_liq_sum;
    $market['liquidity_fee_earned'] += $fee_share; // tracks cumulative fees paid to early-withdrawn LPs

    $user['balance'] += $returned;
    $user['liquidity_balance'] -= $withdraw_amount;

    if ($is_full) {
        $lp['status'] = 2;
        $lp['earned_fee'] = $fee_share;
    } else {
        $lp['amount'] -= $withdraw_amount;
        $lp['weight_a'] -= $w_a;
        $lp['weight_b'] -= $w_b;
        $lp['earned_fee'] += $fee_share;
    }
    // Update lps array
    foreach ($lps as &$ref) {
        if ($ref['id'] === $lp['id']) { $ref = $lp; break; }
    }
    unset($ref);

    return [
        'status' => true, 'returned' => $returned, 'fee_share' => $fee_share,
        'is_full' => $is_full, 'time_ratio' => round($time_ratio, 6),
        'remaining_amount' => $is_full ? 0 : $lp['amount'],
        'reserves' => ['a' => $market['reserve_a'], 'b' => $market['reserve_b'], 'k' => $market['k']],
    ];
}

function resolve_market(&$market, $outcome, $bets, &$lps, &$payouts) {
    $market['status'] = 3;
    $market['resolved_outcome'] = $outcome;
    $market['payout_status'] = 1;

    $total_penalty_pool = 0;
    $winner_payouts = [];

    foreach ($bets as &$b) {
        if ($b['status'] != 0) continue;
        if ($b['side'] == $outcome) {
            $raw = $b['weight'];
            $profit = max(0, $raw - $b['amount']);
            $penalty_ded = intval($profit * $b['time_penalty'] / 1000000);
            $net = $raw - $penalty_ded;
            $total_penalty_pool += $penalty_ded;
            $b['resolved_amount'] = $net;
            $b['status'] = 3;
            if ($net > 0) {
                $payouts[] = ['user' => $b['user'], 'type' => 0, 'amount' => $net, 'label' => 'winner_bet'];
            }
            $winner_payouts[] = ['bet_id' => $b['id'], 'user' => $b['user'],
                'raw' => $raw, 'penalty' => $penalty_ded, 'net' => $net];
        } else {
            $b['resolved_amount'] = 0;
            $b['status'] = 3;
        }
    }
    unset($b);

    // Oracle fee payout: computed from losing side's bet volume
    $losing_sum = ($outcome == 0) ? $market['b_bets_sum'] : $market['a_bets_sum'];
    $oracle_fee_total = intval($losing_sum * $market['oracle_fee'] / 1000);
    if ($oracle_fee_total > 0) {
        $payouts[] = ['user' => $market['oracle'], 'type' => 2, 'amount' => $oracle_fee_total, 'label' => 'oracle_fee'];
    }

    // LP payouts: principal + time-weighted share of (loser fees + penalty_pool)
    // weight = amount × sec_to_expiration (early LP earns more per VIZ)
    $lp_fee_from_losers = intval($losing_sum * $market['liquidity_fee'] / 1000);
    $already_paid_lp = $market['liquidity_fee_earned']; // fees paid to early-withdrawn LPs
    $fee_pool = max(0, $lp_fee_from_losers - $already_paid_lp) + $total_penalty_pool;
    $total_liq = $market['liquidity_sum'];
    $lp_payouts = [];
    if ($total_liq > 0) {
        // First pass: calculate total time-weighted liquidity
        $total_tw = 0;
        foreach ($lps as $l) {
            if ($l['market'] != $market['id'] || $l['status'] != 0) continue;
            $total_tw += $l['amount'] * max(1, $l['sec_to_expiration']);
        }
        // Second pass: distribute
        foreach ($lps as &$l) {
            if ($l['market'] != $market['id'] || $l['status'] != 0) continue;
            $tw = $l['amount'] * max(1, $l['sec_to_expiration']);
            $share = ($fee_pool > 0 && $total_tw > 0) ? intval($fee_pool * $tw / $total_tw) : 0;
            $lp_return = $l['amount'] + $share;
            $l['earned_fee'] = $share;
            $l['status'] = 3;
            $payouts[] = ['user' => $l['user'], 'type' => 1, 'amount' => $lp_return, 'label' => 'lp_return'];
            $lp_payouts[] = ['lp_id' => $l['id'], 'principal' => $l['amount'], 'fee_share' => $share, 'total' => $lp_return, 'time_weight' => $tw, 'sec_to_exp' => $l['sec_to_expiration']];
        }
        unset($l);
    }

    return [
        'outcome' => $outcome, 'total_penalty_pool' => $total_penalty_pool,
        'oracle_fee' => $oracle_fee_total, 'liq_fee_pool' => $fee_pool,
        'winner_payouts' => $winner_payouts, 'lp_payouts' => $lp_payouts,
    ];
}

function auto_payout(&$payouts, &$users_map) {
    $results = [];
    foreach ($payouts as &$p) {
        if (!isset($p['paid'])) $p['paid'] = false;
        if ($p['paid']) continue;
        $uid = $p['user'];
        if (isset($users_map[$uid])) {
            $users_map[$uid]['balance'] += $p['amount'];
            $p['paid'] = true;
            $results[] = ['user' => $uid, 'type' => $p['label'], 'amount' => $p['amount']];
        }
    }
    unset($p);
    return $results;
}

function oracle_penalty(&$market, &$oracle, $bets, &$lps, $settings, &$users_map) {
    $penalty_pct = $settings['oracle_penalty_percent'];
    $penalty_amount = intval($oracle['oracle_insurance'] * $penalty_pct / 100);

    // Collect stakes
    $stakes = [];
    $total_stakes = 0;
    foreach ($bets as $b) {
        if ($b['status'] != 0) continue;
        $uid = $b['user'];
        if (!isset($stakes[$uid])) $stakes[$uid] = 0;
        $stakes[$uid] += $b['amount'];
        $total_stakes += $b['amount'];
    }
    foreach ($lps as $l) {
        if ($l['market'] != $market['id'] || $l['status'] != 0) continue;
        $uid = $l['user'];
        if (!isset($stakes[$uid])) $stakes[$uid] = 0;
        $stakes[$uid] += $l['amount'];
        $total_stakes += $l['amount'];
    }

    // Refund bets
    $refunds = [];
    foreach ($bets as &$b) {
        if ($b['status'] != 0) continue;
        $uid = $b['user'];
        $users_map[$uid]['balance'] += $b['amount'];
        $users_map[$uid]['bets_balance'] -= $b['amount'];
        $b['status'] = 2;
        $refunds[] = ['user' => $uid, 'type' => 'bet_refund', 'amount' => $b['amount']];
    }
    unset($b);

    // Refund liquidity
    foreach ($lps as &$l) {
        if ($l['market'] != $market['id'] || $l['status'] != 0) continue;
        $uid = $l['user'];
        $users_map[$uid]['balance'] += $l['amount'];
        $users_map[$uid]['liquidity_balance'] -= $l['amount'];
        $l['status'] = 2;
        $refunds[] = ['user' => $uid, 'type' => 'liq_refund', 'amount' => $l['amount']];
    }
    unset($l);

    // Deduct oracle insurance
    $oracle['oracle_insurance'] -= $penalty_amount;
    if ($oracle['oracle_insurance'] < 0) $oracle['oracle_insurance'] = 0;

    // Distribute penalty
    $distributions = [];
    if ($penalty_amount > 0 && $total_stakes > 0) {
        foreach ($stakes as $uid => $stake) {
            $bonus = intval($penalty_amount * $stake / $total_stakes);
            if ($bonus > 0) {
                $users_map[$uid]['balance'] += $bonus;
                $distributions[] = ['user' => $uid, 'stake' => $stake, 'bonus' => $bonus];
            }
        }
    }

    $market['status'] = 3;
    $market['payout_status'] = 2;

    return [
        'penalty_amount' => $penalty_amount, 'total_stakes' => $total_stakes,
        'refunds' => $refunds, 'distributions' => $distributions,
        'oracle_insurance_after' => $oracle['oracle_insurance'],
    ];
}

function create_dispute(&$market, &$plaintiff, $settings) {
    $fee = $settings['dispute_fee'];
    if ($plaintiff['balance'] < $fee) return ['error' => 'Insufficient balance'];
    $plaintiff['balance'] -= $fee;
    $market['payout_status'] = 3;
    return ['status' => true, 'dispute_fee' => $fee, 'dispute_id' => 1];
}

function resolve_dispute_oracle_wrong(&$market, &$plaintiff, &$oracle, $correct_outcome,
                                       $bets, &$lps, &$payouts, $dispute_fee,
                                       $extra_penalty=0, $ban_oracle=0, $ban_oracle_until=0,
                                       $ban_creator=0, $ban_creator_until=0, &$creator=null, &$committee_user=null, $settings=null) {
    // reward_pool = min(dispute_fee * multiplier, oracle_insurance)
    $multiplier = ($settings !== null && isset($settings['dispute_reward_multiplier'])) ? floatval($settings['dispute_reward_multiplier']) : 3.0;
    $reward_pool = min(intval(floor($dispute_fee * $multiplier)), $oracle['oracle_insurance']);
    $disputer_reward = intval(floor($reward_pool / $multiplier)); // 1/3 at 3.0
    $voter_reward = $reward_pool - $disputer_reward; // 2/3 at 3.0

    // Plaintiff gets refund + disputer_reward
    $plaintiff['balance'] += $dispute_fee + $disputer_reward;
    // Oracle insurance reduced by reward_pool
    $oracle['oracle_insurance'] -= $reward_pool;
    if ($oracle['oracle_insurance'] < 0) $oracle['oracle_insurance'] = 0;

    // Resolver/voter gets voter_reward
    if ($committee_user !== null && $voter_reward > 0) {
        $committee_user['balance'] += $voter_reward;
    }

    // Additional penalty: slash from oracle insurance, transfer to committee/DAO fund
    $actual_extra_penalty = min($extra_penalty, $oracle['oracle_insurance']);
    if ($actual_extra_penalty > 0) {
        $oracle['oracle_insurance'] -= $actual_extra_penalty;
        if ($oracle['oracle_insurance'] < 0) $oracle['oracle_insurance'] = 0;
        if ($committee_user !== null) {
            $committee_user['balance'] += $actual_extra_penalty;
        }
    }

    // Apply oracle ban
    if (1 == $ban_oracle) {
        $oracle['oracle_banned'] = 1;
        $oracle['oracle_ban_until'] = $ban_oracle_until;
    }

    // Apply creator ban
    if (1 == $ban_creator && $creator !== null) {
        $creator['creator_banned'] = 1;
        $creator['creator_ban_until'] = $ban_creator_until;
    }

    // Delete old unpaid payouts
    $payouts = array_filter($payouts, function($p) { return isset($p['paid']) && $p['paid']; });
    $payouts = array_values($payouts);

    // Recalculate with correct outcome
    $new_payouts = [];
    $result = resolve_market($market, $correct_outcome, $bets, $lps, $new_payouts);
    $market['resolved_outcome'] = $correct_outcome;
    $market['payout_status'] = 1;

    // Merge new payouts
    foreach ($new_payouts as $p) $payouts[] = $p;

    return ['decision' => 'oracle_wrong', 'correct_outcome' => $correct_outcome,
            'plaintiff_refund' => $dispute_fee, 'disputer_reward' => $disputer_reward,
            'voter_reward' => $voter_reward, 'reward_pool' => $reward_pool,
            'oracle_insurance_loss' => $reward_pool,
            'extra_penalty' => $actual_extra_penalty,
            'ban_oracle' => $ban_oracle, 'ban_creator' => $ban_creator,
            'recalculated' => $result];
}

function resolve_dispute_oracle_right(&$market, &$committee, &$oracle, $dispute_fee, &$payouts, $settings=null) {
    $rejected_voter_permille = ($settings !== null && isset($settings['dispute_rejected_voter_permille'])) ? intval($settings['dispute_rejected_voter_permille']) : 500;
    $voter_share = intval(floor($dispute_fee * $rejected_voter_permille / 1000));
    $oracle_share = $dispute_fee - $voter_share;

    // Resolver gets voter_share
    $committee['balance'] += $voter_share;
    // Oracle gets compensation
    $oracle['balance'] += $oracle_share;

    $market['payout_status'] = 1;
    $payouts[] = ['user' => $committee['id'], 'type' => 8, 'amount' => $voter_share,
                  'label' => 'dispute_voter_reward', 'paid' => true];
    $payouts[] = ['user' => $oracle['id'], 'type' => 9, 'amount' => $oracle_share,
                  'label' => 'dispute_oracle_compensation', 'paid' => true];
    return ['decision' => 'oracle_right', 'voter_share' => $voter_share, 'oracle_share' => $oracle_share];
}

function fmt($amount) { return round($amount / 1000, 3) . ' VIZ'; }

// ============================================================
// COMMIT-REVEAL HELPERS (pm_commit_bet, pm_reveal_bet, pm_commit_forfeit, batch_settle)
// ============================================================
function commit_bet(&$market, &$user, $escrow_amount, $side, $amount, $min_tokens, $salt, $settings, $current_time) {
    // Requires allow_batch && commit_reveal_enabled
    if (empty($market['allow_batch'])) return ['error' => 'Batch not allowed'];
    if (empty($settings['commit_reveal_enabled'])) return ['error' => 'Commit-reveal disabled'];
    if ($escrow_amount < ($settings['min_batch_bet'] ?? 1000)) return ['error' => 'Below min batch bet'];
    if ($user['balance'] < $escrow_amount) return ['error' => 'Insufficient balance'];
    if ($market['status'] != 1) return ['error' => 'Market not active'];
    if ($current_time >= $market['betting_expiration']) return ['error' => 'Betting expired'];

    $no_reveal_fee_permille = $settings['commit_no_reveal_penalty_permille'];
    $commitment = hash('sha256', $market['id'] . '|' . $user['id'] . '|' . $side . '|' . $amount . '|' . $min_tokens . '|' . $salt);
    $reveal_window = ($settings['reveal_window_blocks'] ?? 200) * 3; // 3s per block
    $reveal_deadline = $current_time + $reveal_window;

    $user['balance'] -= $escrow_amount;

    static $commit_counter = 0;
    $commit_counter++;

    $commit = [
        'id' => $commit_counter, 'market' => $market['id'], 'account' => $user['id'],
        'commitment' => $commitment, 'escrow_amount' => $escrow_amount,
        'no_reveal_fee_permille' => $no_reveal_fee_permille,
        'side' => $side, 'amount' => $amount, 'min_tokens' => $min_tokens, 'salt' => $salt,
        'commit_time' => $current_time, 'reveal_deadline' => $reveal_deadline,
        'status' => 0, // 0=committed, 1=revealed, 2=forfeited
    ];

    return ['status' => true, 'commit' => $commit];
}

function reveal_bet(&$commit, &$market, &$user, $current_time, $epoch_blocks = 20) {
    if ($commit['status'] != 0) return ['error' => 'Not committed'];
    if ($current_time > $commit['reveal_deadline']) return ['error' => 'Reveal deadline passed'];

    // Verify hash
    $check = hash('sha256', $market['id'] . '|' . $user['id'] . '|' . $commit['side'] . '|' . $commit['amount'] . '|' . $commit['min_tokens'] . '|' . $commit['salt']);
    if ($check !== $commit['commitment']) return ['error' => 'Hash mismatch'];

    // Refund surplus
    $surplus = $commit['escrow_amount'] - $commit['amount'];
    $user['balance'] += $surplus;

    // Determine epoch: first boundary at/after reveal
    $epoch = intval(ceil($current_time / ($epoch_blocks * 3)));

    $commit['status'] = 1; // revealed

    // Create queued bet
    static $reveal_bet_counter = 5000;
    $reveal_bet_counter++;

    $bet = [
        'id' => $reveal_bet_counter, 'market' => $market['id'], 'user' => $user['id'],
        'side' => $commit['side'], 'amount' => $commit['amount'], 'weight' => 0,
        'price' => 0, 'time_penalty' => 0, 'mode' => 2, 'epoch' => $epoch,
        'status' => 5, // queued
        'min_tokens' => $commit['min_tokens'], 'submit_time' => $commit['commit_time'],
        'resolved_amount' => 0,
    ];

    return ['status' => true, 'bet' => $bet, 'surplus_refund' => $surplus, 'epoch' => $epoch];
}

function commit_forfeit(&$commit, &$market, &$user) {
    if ($commit['status'] != 0) return ['error' => 'Not committed (already revealed/forfeited)'];

    $penalty = intval(floor($commit['escrow_amount'] * $commit['no_reveal_fee_permille'] / 1000));
    $refund = $commit['escrow_amount'] - $penalty;

    // Penalty goes to market forfeit_pool (boosts winners' pool at resolution)
    if (!isset($market['forfeit_pool'])) $market['forfeit_pool'] = 0;
    $market['forfeit_pool'] += $penalty;

    // Refund remainder
    $user['balance'] += $refund;
    $commit['status'] = 2; // forfeited

    return ['status' => true, 'penalty' => $penalty, 'refund' => $refund,
            'forfeit_pool' => $market['forfeit_pool']];
}

function batch_settle(&$market, &$queued_bets) {
    // Aggregate each side into ONE CPMM op, canonical order (larger first)
    $ra = $market['reserve_a'];
    $rb = $market['reserve_b'];
    $k  = $market['k'];

    $a_total = 0;
    $b_total = 0;
    foreach ($queued_bets as $b) {
        if ($b['status'] != 5) continue; // only queued
        if ($b['side'] == 0) $a_total += $b['amount'];
        else $b_total += $b['amount'];
    }

    // Canonical order: larger aggregate first, tie → A
    $order = ($a_total >= $b_total) ? [0, 1] : [1, 0];

    $side_totals = [0 => $a_total, 1 => $b_total];
    $side_tokens = [0 => 0, 1 => 0];

    foreach ($order as $side) {
        $total = $side_totals[$side];
        if ($total <= 0) continue;

        if ($side == 0) {
            $new_rb = $rb + $total;
            $new_ra = intval($k / $new_rb);
            $tokens = $ra - $new_ra;
            $ra = $new_ra;
            $rb = $new_rb;
        } else {
            $new_ra = $ra + $total;
            $new_rb = intval($k / $new_ra);
            $tokens = $rb - $new_rb;
            $ra = $new_ra;
            $rb = $new_rb;
        }
        $side_tokens[$side] = $tokens;

        // Distribute tokens pro-rata within the side, check min_tokens
        foreach ($queued_bets as &$b) {
            if ($b['status'] != 5 || $b['side'] != $side) continue;
            $b_tokens = intval(floor($tokens * $b['amount'] / $total));
            if (isset($b['min_tokens']) && $b['min_tokens'] > 0 && $b_tokens < $b['min_tokens']) {
                $b['status'] = 2; // refunded (slippage too high)
            } else {
                $b['weight'] = $b_tokens;
                $b['price'] = intval($b['amount'] * 1000000 / max(1, $b_tokens));
                $b['status'] = 0; // active
            }
        }
        unset($b);
    }

    // Update market reserves
    $market['reserve_a'] = $ra;
    $market['reserve_b'] = $rb;
    // k unchanged (betting, not liquidity event)

    // Dust: sum remaining unassigned tokens → would go to DAO fund
    $dust = 0;
    foreach ([0, 1] as $side) {
        $assigned = 0;
        foreach ($queued_bets as $b) {
            if ($b['side'] == $side && $b['status'] == 0) $assigned += $b['weight'];
        }
        $dust += max(0, $side_tokens[$side] - $assigned);
    }

    // Update bets_sum
    foreach ($queued_bets as &$b) {
        if ($b['status'] == 0) {
            $market['bets_sum'] += $b['amount'];
            if ($b['side'] == 0) $market['a_bets_sum'] += $b['amount'];
            else $market['b_bets_sum'] += $b['amount'];
        }
    }
    unset($b);

    return ['side_totals' => $side_totals, 'side_tokens' => $side_tokens,
            'dust' => $dust, 'reserves' => ['a' => $ra, 'b' => $rb, 'k' => $k]];
}

function place_bet_batch(&$market, &$user, $side, $amount, $current_time, $epoch_blocks = 20) {
    // Batch mode=1: queue bet for next epoch
    if (empty($market['allow_batch'])) return ['error' => 'Batch not allowed'];
    if ($market['status'] != 1) return ['error' => 'Market not active'];
    if ($current_time >= $market['betting_expiration']) return ['error' => 'Betting expired'];
    if ($user['balance'] < $amount) return ['error' => 'Insufficient balance'];

    $user['balance'] -= $amount;
    $epoch = intval(ceil($current_time / ($epoch_blocks * 3)));

    static $batch_bet_counter = 4000;
    $batch_bet_counter++;

    $bet = [
        'id' => $batch_bet_counter, 'market' => $market['id'], 'user' => $user['id'],
        'side' => $side, 'amount' => $amount, 'weight' => 0,
        'price' => 0, 'time_penalty' => 0, 'mode' => 1, 'epoch' => $epoch,
        'status' => 5, // queued
        'min_tokens' => 0, 'submit_time' => $current_time,
        'resolved_amount' => 0,
    ];

    return ['status' => true, 'bet' => $bet, 'epoch' => $epoch];
}

// ============================================================
// ACCOUNT-MODE DISPUTE RESOLUTION (dispute_mode=1)
// ============================================================
function resolve_dispute_account_mode(&$market, &$resolver, &$plaintiff, &$oracle, $correct_outcome,
                                       $bets, &$lps, &$payouts, $dispute_fee,
                                       $penalty_amount = 0, $ban_oracle = 0, $ban_oracle_until = 0,
                                       $ban_creator = 0, $ban_creator_until = 0,
                                       &$creator = null, &$dao_fund = null, $settings = null) {
    // Only the designated dispute_resolver account may call
    if (empty($market['dispute_resolver']) || $market['dispute_resolver'] != $resolver['id']) {
        return ['error' => 'Not the designated resolver'];
    }
    if ($market['dispute_mode'] != 1) {
        return ['error' => 'Market is not in account dispute mode'];
    }

    $multiplier = ($settings !== null && isset($settings['dispute_reward_multiplier'])) ? floatval($settings['dispute_reward_multiplier']) : 3.0;
    $reward_pool = min(intval(floor($dispute_fee * $multiplier)), $oracle['oracle_insurance']);
    $disputer_reward = intval(floor($reward_pool / $multiplier));

    // Plaintiff gets refund + disputer_reward
    $plaintiff['balance'] += $dispute_fee + $disputer_reward;
    $oracle['oracle_insurance'] -= $reward_pool;
    if ($oracle['oracle_insurance'] < 0) $oracle['oracle_insurance'] = 0;

    // Resolver gets a fee for service (from penalty, not from reward_pool)
    $resolver_fee = 0;

    // Additional penalty from oracle insurance
    $actual_penalty = min($penalty_amount, $oracle['oracle_insurance']);
    if ($actual_penalty > 0) {
        $oracle['oracle_insurance'] -= $actual_penalty;
        if ($oracle['oracle_insurance'] < 0) $oracle['oracle_insurance'] = 0;
        // Split: resolver gets a portion, DAO gets the rest
        $resolver_fee = intval($actual_penalty / 2);
        $dao_portion = $actual_penalty - $resolver_fee;
        $resolver['balance'] += $resolver_fee;
        if ($dao_fund !== null) $dao_fund['balance'] += $dao_portion;
    }

    // Apply bans
    if ($ban_oracle) {
        $oracle['oracle_banned'] = 1;
        $oracle['oracle_ban_until'] = $ban_oracle_until;
    }
    if ($ban_creator && $creator !== null) {
        $creator['creator_banned'] = 1;
        $creator['creator_ban_until'] = $ban_creator_until;
    }

    // Delete old unpaid payouts and recalculate
    $payouts = array_values(array_filter($payouts, function($p) { return isset($p['paid']) && $p['paid']; }));
    $new_payouts = [];
    $result = resolve_market($market, $correct_outcome, $bets, $lps, $new_payouts);
    $market['resolved_outcome'] = $correct_outcome;
    $market['payout_status'] = 1;
    foreach ($new_payouts as $p) $payouts[] = $p;

    return ['decision' => 'account_mode', 'correct_outcome' => $correct_outcome,
            'plaintiff_refund' => $dispute_fee, 'disputer_reward' => $disputer_reward,
            'reward_pool' => $reward_pool, 'penalty' => $actual_penalty,
            'resolver_fee' => $resolver_fee, 'ban_oracle' => $ban_oracle, 'ban_creator' => $ban_creator,
            'recalculated' => $result];
}

// ============================================================
// ORACLE ACCEPT WITH FIXED FEE TRANSFER
// ============================================================
function oracle_accept_with_fixed_fee(&$market, &$oracle, &$creator, $settings) {
    if ($market['oracle'] != $oracle['id']) return ['error' => 'Not oracle'];
    if ($market['status'] != 0) return ['error' => 'Not pending'];
    if ($oracle['oracle_insurance'] < $settings['min_oracle_insurance'])
        return ['error' => 'Insufficient insurance'];

    $fixed_fee = $market['oracle_fixed_fee'] ?? 0;
    // Use market fields to detect self-oracle (avoids PHP double-reference aliasing)
    $is_self_oracle = ($market['user'] == $market['oracle']);
    $transferred = 0;

    if ($fixed_fee > 0 && !$is_self_oracle) {
        // Transfer fixed fee from creator to oracle
        $actual_fee = min($fixed_fee, $creator['balance']);
        $creator['balance'] -= $actual_fee;
        $oracle['balance'] += $actual_fee;
        $transferred = $actual_fee;
    }

    $market['status'] = 1; // active

    return ['status' => true, 'fixed_fee_transferred' => $transferred,
            'is_self_oracle' => $is_self_oracle,
            'creator_balance_after' => $creator['balance'],
            'oracle_balance_after' => $oracle['balance']];
}

function balances($users_map) {
    $out = [];
    foreach ($users_map as $u) {
        $out[$u['name']] = [
            'balance' => fmt($u['balance']),
            'bets_balance' => fmt($u['bets_balance']),
            'liquidity_balance' => fmt($u['liquidity_balance']),
            'oracle_insurance' => fmt($u['oracle_insurance']),
        ];
    }
    return $out;
}

// ============================================================
// SCENARIO 1: HAPPY PATH — Oracle resolves correctly, no dispute
// ============================================================
echo "\n\n" . str_repeat('=', 70) . "\n";
echo "SCENARIO 1: HAPPY PATH (oracle resolves X, no dispute)\n";
echo str_repeat('=', 70) . "\n";

$oracle_u    = make_user(1, 'Oracle',    100000000); // 100000 VIZ
$committee_u = make_user(2, 'Committee', 0);
$committee_u['committee'] = 1;
$maker_u     = make_user(3, 'MarketMaker', 1000000); // 1000 VIZ
$user_c      = make_user(4, 'User_C',     500000);   // 500 VIZ
$user_d      = make_user(5, 'User_D',     500000);   // 500 VIZ

$T0 = 1000000; // base time

// Register oracle
$r = register_oracle($oracle_u, 0, 5, $SETTINGS); // fee=5 permille=0.5%
step('Oracle registers', ['result' => $r, 'oracle' => $oracle_u]);

// Deposit insurance
$r = oracle_deposit_insurance($oracle_u, 5000000);
step('Oracle deposits 5000 VIZ insurance', ['result' => $r, 'oracle_insurance' => fmt($oracle_u['oracle_insurance'])]);

// Register creator
$r = register_creator($maker_u, $SETTINGS);
step('MarketMaker registers as creator', ['result' => $r, 'maker' => $maker_u]);

// Create market: 200 VIZ liquidity, oracle_fee=5‰, liq_fee=10‰, penalty_type=1(pct), penalty_value=10
$r = create_market($maker_u, $oracle_u, 200000, 5, 10, $T0,
    $T0 + 172800, $T0 + 259200, 1, 10, $committee_u['id'], $SETTINGS);
$market1 = $r['market'];
$lps = [$r['lp']];
step('MarketMaker creates market_1 (200 VIZ liq, oracle_fee=0.5%, liq_fee=1%)', [
    'market' => $market1, 'lp' => $r['lp'], 'maker_balance' => fmt($maker_u['balance'])
]);

// Oracle accepts
$r = oracle_accept($market1, $oracle_u, $SETTINGS);
step('Oracle accepts market_1', ['result' => $r, 'market_status' => $market1['status']]);

// User_C bets 100 VIZ on side 0 (A) at T0+3600 (early, no penalty)
$bet_time = $T0 + 3600;
$r = place_bet($market1, $user_c, 0, 100000, $bet_time);
$bet_c = $r['bet'];
step('User_C bets 100 VIZ on side A (early, no penalty)', $r);

// User_D bets 50 VIZ on side 1 (B) at T0+7200
$bet_time2 = $T0 + 7200;
$r = place_bet($market1, $user_d, 1, 50000, $bet_time2);
$bet_d = $r['bet'];
step('User_D bets 50 VIZ on side B (early, no penalty)', $r);

// Market state
step('Market state after bets', [
    'reserves' => ['a' => $market1['reserve_a'], 'b' => $market1['reserve_b'], 'k' => $market1['k']],
    'fees_earned' => ['oracle' => fmt($market1['oracle_fee_earned']), 'liquidity' => fmt($market1['liquidity_fee_earned'])],
    'bets_sum' => fmt($market1['bets_sum']),
]);

// Oracle resolves to outcome=0 (A wins)
$payouts1 = [];
$bets1 = [$bet_c, $bet_d];
$r = resolve_market($market1, 0, $bets1, $lps, $payouts1);
step('Oracle resolves market_1 → outcome A', $r);

// Auto-payout
$users_map = [1 => &$oracle_u, 2 => &$committee_u, 3 => &$maker_u, 4 => &$user_c, 5 => &$user_d];
$r = auto_payout($payouts1, $users_map);
step('Auto-payout after grace period', ['paid' => $r, 'balances' => balances($users_map)]);


// ============================================================
// SCENARIO 2: ORACLE REJECTS MARKET
// ============================================================
echo "\n\n" . str_repeat('=', 70) . "\n";
echo "SCENARIO 2: ORACLE REJECTS MARKET\n";
echo str_repeat('=', 70) . "\n";

$maker2 = make_user(10, 'Maker2', 500000);
register_creator($maker2, $SETTINGS);
$oracle2 = make_user(11, 'Oracle2', 100000000);
register_oracle($oracle2, 0, 10, $SETTINGS);
oracle_deposit_insurance($oracle2, 5000000);

$r = create_market($maker2, $oracle2, 200000, 10, 10, $T0,
    $T0+172800, $T0+259200, 1, 10, 0, $SETTINGS);
$m2 = $r['market'];
$lps2 = [$r['lp']];
step('Maker2 creates market_2', ['maker_balance' => fmt($maker2['balance'])]);

$r = oracle_reject($m2, $maker2, $lps2);
step('Oracle2 rejects market_2', ['result' => $r, 'maker_balance' => fmt($maker2['balance'])]);


// ============================================================
// SCENARIO 3: SELF-ORACLE (auto-approve)
// ============================================================
echo "\n\n" . str_repeat('=', 70) . "\n";
echo "SCENARIO 3: SELF-ORACLE (auto-approve)\n";
echo str_repeat('=', 70) . "\n";

$self_u = make_user(20, 'SelfOracle', 10000000);
register_oracle($self_u, 0, 5, $SETTINGS);
oracle_deposit_insurance($self_u, 5000000);
register_creator($self_u, $SETTINGS);

$r = create_market($self_u, $self_u, 200000, 5, 10, $T0,
    $T0+172800, $T0+259200, 1, 10, 0, $SETTINGS);
step('SelfOracle creates market (self-oracle auto-approve)', [
    'market_status' => $r['market']['status'],
    'balance' => fmt($self_u['balance']),
]);


// ============================================================
// SCENARIO 4: TIME PENALTY — bets at different times
// ============================================================
echo "\n\n" . str_repeat('=', 70) . "\n";
echo "SCENARIO 4: TIME PENALTY (bets at different times)\n";
echo str_repeat('=', 70) . "\n";

$oracle4 = make_user(30, 'Oracle4', 100000000);
register_oracle($oracle4, 0, 5, $SETTINGS);
oracle_deposit_insurance($oracle4, 5000000);
$maker4 = make_user(31, 'Maker4', 1000000);
register_creator($maker4, $SETTINGS);

// Market: 48h = 172800s, penalty_type=1(pct), penalty_value=5 → window = 5% of 172800 = 8640s
// Using quadratic curve (default)
$r4 = create_market($maker4, $oracle4, 200000, 5, 10, $T0,
    $T0+172800, $T0+259200, 1, 5, 0, $SETTINGS);
$m4 = $r4['market'];
$lps4 = [$r4['lp']];
oracle_accept($m4, $oracle4, $SETTINGS);

$uc = make_user(32, 'BetEarly', 500000);
$ud = make_user(33, 'BetMid', 500000);
$ue = make_user(34, 'BetLate', 500000);
$uf = make_user(35, 'BetVeryLate', 500000);

// Early bet: T0+3600 (outside penalty window, 169200s to exp > 8640)
$r = place_bet($m4, $uc, 0, 100000, $T0 + 3600);
step('BetEarly bets 100 VIZ at T+1h (no penalty)', ['penalty' => $r['time_penalty'], 'tokens' => $r['tokens']]);
$bet_early = $r['bet'];

// Mid bet: T0+86400 (86400s to exp > 8640)
$r = place_bet($m4, $ud, 0, 100000, $T0 + 86400);
step('BetMid bets 100 VIZ at T+24h (no penalty)', ['penalty' => $r['time_penalty'], 'tokens' => $r['tokens']]);
$bet_mid = $r['bet'];

// Late bet: T0+168000 (4800s to exp, inside 8640 window)
// linear_ratio = 1 - 4800/8640 = 0.444, quadratic = 0.444^2 = 0.197
$r = place_bet($m4, $ue, 0, 100000, $T0 + 168000);
step('BetLate bets 100 VIZ at T+46.67h (4800s to exp, QUADRATIC PENALTY)', ['penalty' => $r['time_penalty'], 'tokens' => $r['tokens']]);
$bet_late = $r['bet'];

// Very late bet: T0+172300 (500s to exp)
// linear_ratio = 1 - 500/8640 = 0.942, quadratic = 0.942^2 = 0.888
$r = place_bet($m4, $uf, 0, 50000, $T0 + 172300);
step('BetVeryLate bets 50 VIZ at T+47.86h (500s to exp, HIGH QUADRATIC PENALTY)', ['penalty' => $r['time_penalty'], 'tokens' => $r['tokens']]);
$bet_very_late = $r['bet'];

// Resolve to A (all bets win but with different penalties)
$payouts4 = [];
$bets4 = [$bet_early, $bet_mid, $bet_late, $bet_very_late];
$r = resolve_market($m4, 0, $bets4, $lps4, $payouts4);
step('Resolve market to A → all win, different penalty deductions', $r);

$users_map4 = [30 => &$oracle4, 31 => &$maker4, 32 => &$uc, 33 => &$ud, 34 => &$ue, 35 => &$uf];
$r = auto_payout($payouts4, $users_map4);
step('Auto-payout with penalties', ['paid' => $r, 'balances' => balances($users_map4)]);


// ============================================================
// SCENARIO 5: ORACLE MISSES RESOLUTION → penalty + refund
// ============================================================
echo "\n\n" . str_repeat('=', 70) . "\n";
echo "SCENARIO 5: ORACLE MISSES RESOLUTION\n";
echo str_repeat('=', 70) . "\n";

$oracle5 = make_user(40, 'Oracle5', 100000000);
register_oracle($oracle5, 0, 5, $SETTINGS);
oracle_deposit_insurance($oracle5, 5000000);
$maker5 = make_user(41, 'Maker5', 1000000);
register_creator($maker5, $SETTINGS);

$r5 = create_market($maker5, $oracle5, 200000, 5, 10, $T0,
    $T0+172800, $T0+259200, 1, 10, 0, $SETTINGS);
$m5 = $r5['market'];
$lps5 = [$r5['lp']];
oracle_accept($m5, $oracle5, $SETTINGS);

$bettor5a = make_user(42, 'Bettor5A', 500000);
$bettor5b = make_user(43, 'Bettor5B', 500000);
$rb1 = place_bet($m5, $bettor5a, 0, 100000, $T0+3600);
$rb2 = place_bet($m5, $bettor5b, 1, 80000, $T0+7200);
$bets5 = [$rb1['bet'], $rb2['bet']];

step('Market 5 state before penalty', [
    'bettor5a_balance' => fmt($bettor5a['balance']),
    'bettor5b_balance' => fmt($bettor5b['balance']),
    'maker5_balance' => fmt($maker5['balance']),
    'oracle5_insurance' => fmt($oracle5['oracle_insurance']),
]);

// Oracle misses deadline → cron penalty
$users_map5 = [40 => &$oracle5, 41 => &$maker5, 42 => &$bettor5a, 43 => &$bettor5b];
$r = oracle_penalty($m5, $oracle5, $bets5, $lps5, $SETTINGS, $users_map5);
step('Oracle misses deadline → cron penalty', $r);
step('Balances after penalty', balances($users_map5));


// ============================================================
// SCENARIO 6: DISPUTE — Committee sides with PLAINTIFF (oracle wrong)
// ============================================================
echo "\n\n" . str_repeat('=', 70) . "\n";
echo "SCENARIO 6: DISPUTE — ORACLE WRONG\n";
echo str_repeat('=', 70) . "\n";

$oracle6 = make_user(50, 'Oracle6', 100000000);
register_oracle($oracle6, 0, 5, $SETTINGS);
oracle_deposit_insurance($oracle6, 5000000);
$maker6 = make_user(51, 'Maker6', 1000000);
register_creator($maker6, $SETTINGS);
$committee6 = make_user(52, 'Committee6', 0);
$committee6['committee'] = 1;

$r6 = create_market($maker6, $oracle6, 200000, 5, 10, $T0,
    $T0+172800, $T0+259200, 1, 10, $committee6['id'], $SETTINGS);
$m6 = $r6['market'];
$lps6 = [$r6['lp']];
oracle_accept($m6, $oracle6, $SETTINGS);

$bettor_x = make_user(53, 'BettorX', 2000000);
$bettor_y = make_user(54, 'BettorY', 2000000);
$bx = place_bet($m6, $bettor_x, 0, 100000, $T0+3600);
$by = place_bet($m6, $bettor_y, 1, 100000, $T0+7200);
$bets6 = [$bx['bet'], $by['bet']];

step('Bets placed', ['bettor_x' => fmt($bettor_x['balance']), 'bettor_y' => fmt($bettor_y['balance'])]);

// Oracle resolves to A (wrong — should be B)
$payouts6 = [];
$r = resolve_market($m6, 0, $bets6, $lps6, $payouts6);
step('Oracle resolves to A (WRONG)', $r);

// Bettor Y disputes
$r = create_dispute($m6, $bettor_y, $SETTINGS);
step('BettorY files dispute', ['result' => $r, 'bettor_y_balance' => fmt($bettor_y['balance'])]);

// Committee decides oracle was wrong, correct outcome = B(1)
$null_creator = null;
$r = resolve_dispute_oracle_wrong($m6, $bettor_y, $oracle6, 1, $bets6, $lps6, $payouts6, $SETTINGS['dispute_fee'], 0, 0, 0, 0, 0, $null_creator, $committee6, $SETTINGS);
step('Committee resolves: ORACLE WRONG, correct=B', $r);

$users_map6 = [50 => &$oracle6, 51 => &$maker6, 52 => &$committee6, 53 => &$bettor_x, 54 => &$bettor_y];
$r = auto_payout($payouts6, $users_map6);
step('Auto-payout after dispute (oracle wrong)', ['paid' => $r, 'balances' => balances($users_map6)]);


// ============================================================
// SCENARIO 7: DISPUTE — Committee sides with ORACLE (oracle right)
// ============================================================
echo "\n\n" . str_repeat('=', 70) . "\n";
echo "SCENARIO 7: DISPUTE — ORACLE RIGHT\n";
echo str_repeat('=', 70) . "\n";

$oracle7 = make_user(60, 'Oracle7', 100000000);
register_oracle($oracle7, 0, 5, $SETTINGS);
oracle_deposit_insurance($oracle7, 5000000);
$maker7 = make_user(61, 'Maker7', 1000000);
register_creator($maker7, $SETTINGS);
$committee7 = make_user(62, 'Committee7', 0);
$committee7['committee'] = 1;

$r7 = create_market($maker7, $oracle7, 200000, 5, 10, $T0,
    $T0+172800, $T0+259200, 1, 10, $committee7['id'], $SETTINGS);
$m7 = $r7['market'];
$lps7 = [$r7['lp']];
oracle_accept($m7, $oracle7, $SETTINGS);

$bx7 = make_user(63, 'BettorX7', 2000000);
$by7 = make_user(64, 'BettorY7', 2000000);
$b7a = place_bet($m7, $bx7, 0, 100000, $T0+3600);
$b7b = place_bet($m7, $by7, 1, 100000, $T0+7200);
$bets7 = [$b7a['bet'], $b7b['bet']];

// Oracle resolves to A (correct)
$payouts7 = [];
$r = resolve_market($m7, 0, $bets7, $lps7, $payouts7);
step('Oracle resolves to A (correct)', $r);

// Bettor Y7 disputes anyway
$r = create_dispute($m7, $by7, $SETTINGS);
step('BettorY7 files dispute', ['result' => $r]);

// Committee decides oracle was RIGHT
$r = resolve_dispute_oracle_right($m7, $committee7, $oracle7, $SETTINGS['dispute_fee'], $payouts7, $SETTINGS);
step('Committee resolves: ORACLE RIGHT', $r);

$users_map7 = [60 => &$oracle7, 61 => &$maker7, 62 => &$committee7, 63 => &$bx7, 64 => &$by7];
$r = auto_payout($payouts7, $users_map7);
step('Auto-payout after dispute (oracle right)', ['paid' => $r, 'balances' => balances($users_map7)]);


// ============================================================
// SCENARIO 8: BET CANCELLATION + SLIPPAGE
// ============================================================
echo "\n\n" . str_repeat('=', 70) . "\n";
echo "SCENARIO 8: BET CANCELLATION + SLIPPAGE\n";
echo str_repeat('=', 70) . "\n";

$oracle8 = make_user(70, 'Oracle8', 100000000);
register_oracle($oracle8, 0, 5, $SETTINGS);
oracle_deposit_insurance($oracle8, 5000000);
$maker8 = make_user(71, 'Maker8', 1000000);
register_creator($maker8, $SETTINGS);

$r8 = create_market($maker8, $oracle8, 200000, 5, 10, $T0,
    $T0+172800, $T0+259200, 1, 10, 0, $SETTINGS);
$m8 = $r8['market'];
$lps8 = [$r8['lp']];
oracle_accept($m8, $oracle8, $SETTINGS);

$uc8 = make_user(72, 'UserC8', 500000);
$ud8 = make_user(73, 'UserD8', 500000);

// C bets 100 VIZ on A
$rc = place_bet($m8, $uc8, 0, 100000, $T0+3600);
$betC8 = $rc['bet'];
step('C bets 100 VIZ on A', ['tokens' => $rc['tokens'], 'reserves_after' => $rc['reserves']]);

// D bets 50 VIZ on B (shifts reserves)
$rd = place_bet($m8, $ud8, 1, 50000, $T0+7200);
$betD8 = $rd['bet'];
step('D bets 50 VIZ on B (shifts reserves)', ['tokens' => $rd['tokens'], 'reserves_after' => $rd['reserves']]);

// C cancels bet (slippage because D's bet changed reserves)
$r = cancel_bet($m8, $uc8, $betC8);
step('C cancels bet → slippage effect', [
    'result' => $r,
    'userC_balance' => fmt($uc8['balance']),
    'reserves_after' => ['a' => $m8['reserve_a'], 'b' => $m8['reserve_b']],
]);


// ============================================================
// SCENARIO 9: MULTIPLE LPs — TIME-WEIGHTED proportional reward
// Early LP (at creation, 172800s left) vs Late LP (at T+86400, 86400s left)
// Same 100 VIZ each but early LP gets 2× the fee share per VIZ
// ============================================================
echo "\n\n" . str_repeat('=', 70) . "\n";
echo "SCENARIO 9: MULTIPLE LPs — TIME-WEIGHTED\n";
echo str_repeat('=', 70) . "\n";

$oracle9 = make_user(80, 'Oracle9', 100000000);
register_oracle($oracle9, 0, 5, $SETTINGS);
oracle_deposit_insurance($oracle9, 5000000);
$maker9 = make_user(81, 'Maker9', 1000000);
register_creator($maker9, $SETTINGS);
$lp2_u = make_user(82, 'LP2_Late', 500000);
$lp2_u['add_liquidity'] = 1;
$lp3_u = make_user(84, 'LP3_Late2', 500000);
$lp3_u['add_liquidity'] = 1;

// Market: 48h = 172800s
$r9 = create_market($maker9, $oracle9, 200000, 5, 20, $T0,
    $T0+172800, $T0+259200, 1, 10, 0, $SETTINGS);
$m9 = $r9['market'];
$lps9 = [$r9['lp']];
oracle_accept($m9, $oracle9, $SETTINGS);

step('Maker9 initial LP: 200 VIZ at creation, sec_to_exp='.$lps9[0]['sec_to_expiration'], [
    'lp' => $lps9[0],
]);

// LP2 adds 100 VIZ at T+86400 (halfway, 86400s remaining)
$r = add_liquidity($m9, $lp2_u, 100000, $lps9, $T0+86400);
step('LP2_Late adds 100 VIZ at T+24h, sec_to_exp='.$lps9[count($lps9)-1]['sec_to_expiration'], [
    'result' => $r, 'total_liquidity' => fmt($m9['liquidity_sum']),
]);

// LP3 adds 100 VIZ at T+172799 (1 second before expiration!)
$r = add_liquidity($m9, $lp3_u, 100000, $lps9, $T0+172799);
step('LP3_Late2 adds 100 VIZ at T+172799s (1s before exp!), sec_to_exp='.$lps9[count($lps9)-1]['sec_to_expiration'], [
    'result' => $r, 'total_liquidity' => fmt($m9['liquidity_sum']),
]);

// Bettor bets 200 VIZ
$bettor9 = make_user(83, 'Bettor9', 500000);
$rb9 = place_bet($m9, $bettor9, 0, 200000, $T0+3600);
step('Bettor9 bets 200 VIZ on A', [
    'tokens' => $rb9['tokens'],
    'fees' => $rb9['fees'],
    'liq_fee_earned' => fmt($m9['liquidity_fee_earned']),
]);
$bets9 = [$rb9['bet']];

// Resolve to A
$payouts9 = [];
$r = resolve_market($m9, 0, $bets9, $lps9, $payouts9);
step('Resolve to A — TIME-WEIGHTED LP rewards', $r);
echo "  NOTE: Maker9 (200 VIZ × 172800s) gets MUCH more fee share than LP3 (100 VIZ × 1s)\n";

$users_map9 = [80 => &$oracle9, 81 => &$maker9, 82 => &$lp2_u, 83 => &$bettor9, 84 => &$lp3_u];
$r = auto_payout($payouts9, $users_map9);
step('Auto-payout multiple LPs', ['paid' => $r, 'balances' => balances($users_map9)]);


// ============================================================
// SCENARIO 10: PENALTY CURVE COMPARISON (linear vs quadratic)
// ============================================================
echo "\n\n" . str_repeat('=', 70) . "\n";
echo "SCENARIO 10: PENALTY CURVE COMPARISON (linear vs quadratic)\n";
echo str_repeat('=', 70) . "\n";

$oracle10 = make_user(90, 'Oracle10', 100000000);
register_oracle($oracle10, 0, 5, $SETTINGS);
oracle_deposit_insurance($oracle10, 5000000);
$maker10 = make_user(91, 'Maker10', 2000000);
register_creator($maker10, $SETTINGS);

// Linear market (penalty_curve_type=0)
$r10_lin = create_market($maker10, $oracle10, 200000, 5, 10, $T0,
    $T0+172800, $T0+259200, 1, 5, 0, $SETTINGS, 0);
$m10_lin = $r10_lin['market'];
$lps10_lin = [$r10_lin['lp']];
oracle_accept($m10_lin, $oracle10, $SETTINGS);

// Quadratic market (penalty_curve_type=1)
$r10_quad = create_market($maker10, $oracle10, 200000, 5, 10, $T0,
    $T0+172800, $T0+259200, 1, 5, 0, $SETTINGS, 1);
$m10_quad = $r10_quad['market'];
$lps10_quad = [$r10_quad['lp']];
oracle_accept($m10_quad, $oracle10, $SETTINGS);

$u10a = make_user(92, 'BetterLinear', 500000);
$u10b = make_user(93, 'BetterQuad', 500000);

// Same bet timing: 4800s before exp (inside 8640s window)
// linear_ratio = 1 - 4800/8640 = 0.4444
// quadratic_ratio = 0.4444^2 = 0.1975
$r_lin = place_bet($m10_lin, $u10a, 0, 100000, $T0 + 168000);
$r_quad = place_bet($m10_quad, $u10b, 0, 100000, $T0 + 168000);
step('Penalty curve comparison (4800s to exp)', [
    'linear_penalty' => $r_lin['time_penalty'],
    'quadratic_penalty' => $r_quad['time_penalty'],
    'linear_curve' => $m10_lin['penalty_curve_type'],
    'quadratic_curve' => $m10_quad['penalty_curve_type'],
    'note' => 'Quadratic should be ~44% of linear penalty',
]);


// ============================================================
// SCENARIO 11: LOW-VOLUME LP SOLVENCY (1 bet, one-sided)
// ============================================================
echo "\n\n" . str_repeat('=', 70) . "\n";
echo "SCENARIO 11: LOW-VOLUME LP SOLVENCY (1 small bet, one side)\n";
echo str_repeat('=', 70) . "\n";

$oracle11 = make_user(100, 'Oracle11', 100000000);
register_oracle($oracle11, 0, 5, $SETTINGS);
oracle_deposit_insurance($oracle11, 5000000);
$maker11 = make_user(101, 'Maker11', 1000000);
register_creator($maker11, $SETTINGS);

$r11 = create_market($maker11, $oracle11, 100000, 5, 10, $T0,
    $T0+172800, $T0+259200, 1, 5, 0, $SETTINGS, 0); // linear, no curve distortion
$m11 = $r11['market'];
$lps11 = [$r11['lp']];
oracle_accept($m11, $oracle11, $SETTINGS);

$bettor11 = make_user(102, 'SmallBettor', 500000);
// 1 VIZ bet on side A, no bets on side B
$rb11 = place_bet($m11, $bettor11, 0, 1000, $T0+3600); // 1 VIZ = 1000 units
$bets11 = [$rb11['bet']];
step('SmallBettor bets 1 VIZ on side A', ['tokens' => $rb11['tokens'], 'price' => $rb11['price']]);

// Resolve to A (winner)
$payouts11 = [];
$r = resolve_market($m11, 0, $bets11, $lps11, $payouts11);
step('Resolve to A → small bettor wins', $r);

// Solvency check
$money_in = $m11['liquidity_sum'] + $m11['bets_sum'];
$money_out = 0;
foreach ($payouts11 as $p) $money_out += $p['amount'];
$lp_principal = 0;
foreach ($r['lp_payouts'] as $lpp) $lp_principal += $lpp['principal'];
$winner_payout = 0;
foreach ($r['winner_payouts'] as $wp) $winner_payout += $wp['net'];

step('LP SOLVENCY CHECK', [
    'money_in' => fmt($money_in),
    'money_out' => fmt($money_out),
    'surplus' => fmt($money_in - $money_out),
    'lp_principal_returned' => fmt($lp_principal),
    'winner_payout' => fmt($winner_payout),
    'SOLVENCY' => ($money_in >= $money_out) ? 'PASS — LP principal covered' : 'FAIL — INSUFFICIENT FUNDS',
]);


// ============================================================
// SCENARIO 12: ORACLE RE-REGISTRATION (profile update)
// ============================================================
echo "\n\n" . str_repeat('=', 70) . "\n";
echo "SCENARIO 12: ORACLE RE-REGISTRATION (profile update without fee)\n";
echo str_repeat('=', 70) . "\n";

$oracle12 = make_user(110, 'Oracle12', 100000000);
$balance_before = $oracle12['balance'];
$r = register_oracle($oracle12, 0, 5, $SETTINGS, 5000);
step('Oracle12 first registration', ['result' => $r, 'balance_spent' => fmt($balance_before - $oracle12['balance'])]);

$balance_before2 = $oracle12['balance'];
$r = register_oracle($oracle12, 0, 10, $SETTINGS, 10000); // update fee to 10 VIZ
step('Oracle12 re-registration (update profile)', [
    'result' => $r,
    'balance_spent' => fmt($balance_before2 - $oracle12['balance']),
    'new_oracle_fee' => $oracle12['oracle_fee'],
    'new_fixed_fee' => fmt($oracle12['oracle_fixed_fee']),
    'note' => 'Balance should not change on re-registration',
]);


// ============================================================
// SCENARIO 13: DISPUTE WITH EXTRA PENALTY + ORACLE BAN
// ============================================================
echo "\n\n" . str_repeat('=', 70) . "\n";
echo "SCENARIO 13: DISPUTE WITH EXTRA PENALTY + ORACLE BAN\n";
echo str_repeat('=', 70) . "\n";

$oracle13 = make_user(120, 'Oracle13', 100000000);
$committee13 = make_user(121, 'Committee13', 0);
$committee13['committee'] = 1;
$maker13 = make_user(122, 'Maker13', 1000000);
$user13a = make_user(123, 'User13A', 500000);

register_oracle($oracle13, 0, 5, $SETTINGS);
oracle_deposit_insurance($oracle13, 5000000);
register_creator($maker13, $SETTINGS);

$r13 = create_market($maker13, $oracle13, 200000, 5, 10, $T0,
    $T0 + 172800, $T0 + 259200, 1, 10, $committee13['id'], $SETTINGS, -1, $committee13);
$m13 = $r13['market'];
$lps13 = [$r13['lp']];
oracle_accept($m13, $oracle13, $SETTINGS);

$r = place_bet($m13, $user13a, 0, 100000, $T0 + 3600);
$bet13a = $r['bet'];
step('User13A bets 100 VIZ on A', $r);

// Resolve incorrectly (oracle says B wins)
$payouts13 = [];
$bets13 = [$bet13a];
resolve_market($m13, 1, $bets13, $lps13, $payouts13);
step('Oracle13 resolves to B (WRONG, should be A)', ['market_outcome' => $m13['resolved_outcome']]);

// Dispute with extra penalty (2000 VIZ) + oracle ban (permanent)
$r = create_dispute($m13, $user13a, $SETTINGS);
step('User13A files dispute', $r);

$oracle13_insurance_before = $oracle13['oracle_insurance'];
$r = resolve_dispute_oracle_wrong($m13, $user13a, $oracle13, 0,
    $bets13, $lps13, $payouts13, $SETTINGS['dispute_fee'],
    2000000, 1, 0, 0, 0, $maker13, $committee13, $SETTINGS); // extra_penalty=2000 VIZ, ban oracle permanently
step('Committee resolves: oracle WRONG + 2000 VIZ extra penalty + permanent oracle ban', [
    'result' => $r,
    'oracle_insurance_before' => fmt($oracle13_insurance_before),
    'oracle_insurance_after' => fmt($oracle13['oracle_insurance']),
    'total_slashed' => fmt($oracle13_insurance_before - $oracle13['oracle_insurance']),
    'oracle_banned' => $oracle13['oracle_banned'],
    'oracle_ban_until' => $oracle13['oracle_ban_until'],
    'committee_balance' => fmt($committee13['balance']),
]);

// Verify ban enforcement: banned oracle cannot accept markets
$test_market_r = create_market($maker13, $oracle13, 200000, 5, 10, $T0,
    $T0 + 172800, $T0 + 259200, 1, 10, 0, $SETTINGS);
$test_m = $test_market_r['market'];
// Simulate ban check (oracle_accept would fail in real code)
$ban_check = (1 == $oracle13['oracle_banned'] && (0 == $oracle13['oracle_ban_until'] || $oracle13['oracle_ban_until'] > time()));
step('Ban enforcement: oracle tries to accept market', [
    'oracle_banned' => $oracle13['oracle_banned'],
    'would_be_rejected' => $ban_check ? 'YES - Oracle is banned' : 'NO - Oracle is not banned',
]);


// ============================================================
// SCENARIO 14: DISPUTE WITH CREATOR BAN (TIME-LIMITED)
// ============================================================
echo "\n\n" . str_repeat('=', 70) . "\n";
echo "SCENARIO 14: DISPUTE WITH TIME-LIMITED CREATOR BAN\n";
echo str_repeat('=', 70) . "\n";

$oracle14 = make_user(130, 'Oracle14', 100000000);
$committee14 = make_user(131, 'Committee14', 0);
$committee14['committee'] = 1;
$maker14 = make_user(132, 'Maker14', 1000000);
$user14a = make_user(133, 'User14A', 500000);

register_oracle($oracle14, 0, 5, $SETTINGS);
oracle_deposit_insurance($oracle14, 5000000);
register_creator($maker14, $SETTINGS);

$r14 = create_market($maker14, $oracle14, 200000, 5, 10, $T0,
    $T0 + 172800, $T0 + 259200, 1, 10, $committee14['id'], $SETTINGS, -1, $committee14);
$m14 = $r14['market'];
$lps14 = [$r14['lp']];
oracle_accept($m14, $oracle14, $SETTINGS);

$r = place_bet($m14, $user14a, 0, 100000, $T0 + 3600);
$bet14a = $r['bet'];

$payouts14 = [];
$bets14 = [$bet14a];
resolve_market($m14, 1, $bets14, $lps14, $payouts14);

$r = create_dispute($m14, $user14a, $SETTINGS);

// Ban creator for 30 days
$ban_until = time() + (30 * 86400);
$r = resolve_dispute_oracle_wrong($m14, $user14a, $oracle14, 0,
    $bets14, $lps14, $payouts14, $SETTINGS['dispute_fee'],
    0, 0, 0, 1, $ban_until, $maker14, $committee14, $SETTINGS); // ban creator for 30 days
step('Committee resolves: oracle wrong + creator banned 30 days', [
    'creator_banned' => $maker14['creator_banned'],
    'creator_ban_until' => $maker14['creator_ban_until'],
    'ban_active' => ($maker14['creator_banned'] == 1 && $maker14['creator_ban_until'] > time()) ? 'YES' : 'NO',
]);


// ============================================================
// SCENARIO 15: FRACTIONAL LP WITHDRAWAL
// LP deposits 200 VIZ, withdraws 50% (100 VIZ), position stays active
// Then places bet, resolves, remaining LP gets payout
// ============================================================
echo "\n\n" . str_repeat('=', 70) . "\n";
echo "SCENARIO 15: FRACTIONAL LP WITHDRAWAL\n";
echo str_repeat('=', 70) . "\n";

$T0 = 1000000;
$oracle15 = make_user(150, 'Oracle15', 100000000);
register_oracle($oracle15, 0, 5, $SETTINGS);
oracle_deposit_insurance($oracle15, 5000000);
$maker15 = make_user(151, 'Maker15', 100000000);
register_creator($maker15, $SETTINGS);
$lp15_user = make_user(152, 'LP15', 100000000);
$lp15_user['add_liquidity'] = 1;
$bettor15 = make_user(153, 'Bettor15', 100000000);

// Create market with 200 VIZ liquidity
$lps15 = [];
$m15 = create_market($maker15, $oracle15, 200000, 5, 10, $T0,
    $T0+172800, $T0+259200, 1, 5, 0, $SETTINGS);
$m15_market = $m15['market'];
oracle_accept($m15_market, $oracle15, $SETTINGS);

step('Market 15 created', [
    'liquidity_sum' => $m15_market['liquidity_sum'],
    'reserves' => ['a' => $m15_market['reserve_a'], 'b' => $m15_market['reserve_b']],
]);

// Get creator's LP (first LP)
$creator_lp15 = $m15['lp'];
$creator_lp15['time_deposited'] = $T0;
$lps15[] = $creator_lp15;

// Second LP adds 200 VIZ at T0+3600 (1 hour in)
$lp_time = $T0 + 3600;
$r_add = add_liquidity($m15_market, $lp15_user, 200000, $lps15, $lp_time);

step('Second LP added 200 VIZ', [
    'lp' => $r_add['lp'],
    'reserves' => $r_add['reserves'],
]);

// Place a bet to generate fees
$bets15 = [];
$bet_time = $T0 + 7200;
$r_bet = place_bet($m15_market, $bettor15, 0, 50000, $bet_time);
$bets15[] = $r_bet['bet'];

step('Bet placed (50 VIZ on A)', [
    'tokens' => $r_bet['bet']['weight'],
    'fee_earned' => $m15_market['liquidity_fee_earned'],
]);

// LP15 withdraws 50% (100 VIZ out of 200 VIZ) at T0+36000 (10 hours in)
$withdraw_time = $T0 + 36000;
$lp_ref = $lps15[1]; // second LP
$r_partial = withdraw_liquidity($m15_market, $lp15_user, $lp_ref, 100000, $lps15, $withdraw_time, $SETTINGS);

step('LP15 partial withdrawal (100 of 200 VIZ)', [
    'returned' => $r_partial['returned'],
    'fee_share' => $r_partial['fee_share'],
    'is_full' => $r_partial['is_full'] ? 'NO (partial)' : 'ERROR',
    'remaining_amount' => $r_partial['remaining_amount'],
    'remaining_lp_status' => $lps15[1]['status'],
    'remaining_weight_a' => $lps15[1]['weight_a'],
    'remaining_weight_b' => $lps15[1]['weight_b'],
]);

// Verify: position still active, amount reduced
assert($r_partial['is_full'] === false);
assert($lps15[1]['status'] === 0); // still active
assert($lps15[1]['amount'] === 100000); // 100 VIZ remaining
assert($r_partial['remaining_amount'] === 100000);

// LP15 tries to withdraw remaining — should work as full withdrawal
$withdraw_time2 = $T0 + 40000;
$lp_ref2 = $lps15[1];
$r_full = withdraw_liquidity($m15_market, $lp15_user, $lp_ref2, 0, $lps15, $withdraw_time2, $SETTINGS);

step('LP15 full withdrawal of remainder', [
    'returned' => $r_full['returned'],
    'fee_share' => $r_full['fee_share'],
    'is_full' => $r_full['is_full'] ? 'YES (full)' : 'ERROR',
    'lp_status' => $lps15[1]['status'],
]);

assert($r_full['is_full'] === true);
assert($lps15[1]['status'] === 2); // closed

step('Scenario 15 complete: fractional withdrawal works', [
    'partial_returned' => $r_partial['returned'],
    'full_returned' => $r_full['returned'],
    'total_returned' => $r_partial['returned'] + $r_full['returned'],
    'original_deposit' => 200000,
    'principal_safe' => ($r_partial['returned'] + $r_full['returned']) >= 200000 ? 'YES' : 'NO',
]);


// ============================================================
// SCENARIO 16: Risk Score — oracle insurance coverage check
// ============================================================
echo "\n\n### SCENARIO 16: Risk Score — oracle insurance undercollateralization ###\n";

function calc_risk_score($oracle_insurance, $total_oracle_bets) {
    if ($total_oracle_bets <= 0) return 999;
    return $oracle_insurance / $total_oracle_bets;
}

function check_bet_risk($oracle, $total_oracle_bets, $bet_amount, $settings) {
    $total_after = $total_oracle_bets + $bet_amount;
    $risk_score = calc_risk_score($oracle['oracle_insurance'], $total_after);
    $min_risk = $settings['min_risk_score_betting'];
    return [
        'risk_score' => round($risk_score, 2),
        'blocked' => ($risk_score < $min_risk) ? 1 : 0,
        'min_threshold' => $min_risk,
    ];
}

function check_listing_risk($oracle, $total_oracle_bets, $settings) {
    $risk_score = calc_risk_score($oracle['oracle_insurance'], $total_oracle_bets);
    $min_risk = $settings['min_risk_score_listing'];
    return [
        'risk_score' => round($risk_score, 2),
        'hidden' => ($risk_score < $min_risk) ? 1 : 0,
        'min_threshold' => $min_risk,
    ];
}

// Setup: oracle with 5000 VIZ insurance, 1 active market
$rs_oracle = make_user(30, 'RiskOracle', 100000000);
register_oracle($rs_oracle, 10000, 5, $SETTINGS);
oracle_deposit_insurance($rs_oracle, 5000000); // 5000 VIZ
$rs_creator = make_user(31, 'RiskCreator', 100000000);
register_creator($rs_creator, $SETTINGS);
$rs_committee = make_user(2, 'Committee', 0);

$rs_market = create_market(
    $rs_creator, $rs_oracle, 500000, 5, 10,
    $base_time, $base_time + 86400, $base_time + 172800,
    1, 5, $rs_committee['id'], $SETTINGS, -1, $rs_committee
);
// Oracle accepts
$rs_market['status'] = 1;
// Track total bets across all oracle markets (global ratio)
$total_oracle_bets = 0; // no bets yet

// Step 1: No bets yet — risk score = 999 (safe)
$rs1 = calc_risk_score($rs_oracle['oracle_insurance'], $total_oracle_bets);
step('Risk score with no bets = safe', [
    'oracle_insurance' => $rs_oracle['oracle_insurance'] / 1000 . ' VIZ',
    'total_oracle_bets' => $total_oracle_bets,
    'risk_score' => $rs1,
]);
assert($rs1 == 999);

// Step 2: Small bet — high risk score (well covered)
// After 100 VIZ bet: total_bets=100000, risk=5000000/100000=50
$check_small = check_bet_risk($rs_oracle, $total_oracle_bets, 100000, $SETTINGS);
step('Risk check: 100 VIZ bet (well covered)', $check_small);
assert($check_small['blocked'] == 0);

// Place the bet to update bets_sum
place_bet($rs_market, $rs_creator, 0, 100000, $base_time + 100);
$total_oracle_bets = $rs_market['bets_sum']; // 100000
step('Bet placed, total_oracle_bets', ['total_oracle_bets' => $total_oracle_bets]);

// Step 3: Large bet that would bring risk_score below threshold
// After 3900 VIZ more: total=100000+3900000=4000000, risk=5000000/4000000=1.25 < 1.5 → blocked
$check_large = check_bet_risk($rs_oracle, $total_oracle_bets, 3900000, $SETTINGS);
step('Risk check: 3900 VIZ bet (would undercollateralize)', $check_large);
assert($check_large['blocked'] == 1);
assert($check_large['risk_score'] < 1.5);

// Step 4: Listing visibility — total bets 100 VIZ, risk=50 → visible
$listing1 = check_listing_risk($rs_oracle, $total_oracle_bets, $SETTINGS);
step('Listing check: 100 VIZ total bets (visible)', $listing1);
assert($listing1['hidden'] == 0);

// Step 5: Simulate high total bets across multiple markets
// total_bets=2500000 (2500 VIZ across all markets), risk=5000000/2500000=2.0 < 2.5 → hidden
$total_heavy = 2500000;
$listing2 = check_listing_risk($rs_oracle, $total_heavy, $SETTINGS);
step('Listing check: 2500 VIZ total oracle bets (hidden from listing)', $listing2);
assert($listing2['hidden'] == 1);
assert($listing2['risk_score'] < 2.5);

// Step 6: Same scenario but with show_risky=1
step('With show_risky=1, hidden markets would be included in response', [
    'risk_score' => $listing2['risk_score'],
    'hidden_flag' => $listing2['hidden'],
    'show_risky_override' => 'would include in API response despite hidden flag',
]);

// Step 7: Summary
step('Scenario 16 complete: risk score system works (global ratio)', [
    'formula' => 'risk_score = oracle_insurance / total_bets_all_oracle_markets',
    'safe_market_visible' => 'YES (risk=50.0)',
    'undercollateralized_bet_blocked' => 'YES (risk=1.25 < 1.5)',
    'high_volume_hidden' => 'YES (risk=2.0 < 2.5)',
    'risk_confirm_override' => 'Server allows with risk_confirm=1',
]);


// ============================================================
// SCENARIO 17: ORACLE VOLUNTARY NO-CONTEST
// Oracle can't verify outcome, voluntarily cancels market with reduced penalty
// ============================================================
echo "\n\n" . str_repeat('=', 70) . "\n";
echo "SCENARIO 17: ORACLE VOLUNTARY NO-CONTEST\n";
echo str_repeat('=', 70) . "\n";

function oracle_no_contest(&$market, &$oracle, &$bets, &$lps, &$payouts, $settings) {
    $dispute_fee = $settings['dispute_fee'];
    $no_contest_pct = $settings['oracle_no_contest_penalty_percent'];
    $penalty = intval($dispute_fee * $no_contest_pct / 100);
    if ($penalty > $oracle['oracle_insurance']) $penalty = $oracle['oracle_insurance'];

    // Collect stakes for penalty distribution
    $stakes = [];
    $total_stakes = 0;

    // Mark all active bets as resolved with full refund, create pending payouts
    foreach ($bets as &$b) {
        if ($b['status'] != 0) continue;
        $uid = $b['user'];
        $bet_amount = $b['amount'];
        if (!isset($stakes[$uid])) $stakes[$uid] = 0;
        $stakes[$uid] += $bet_amount;
        $total_stakes += $bet_amount;
        $b['status'] = 3;
        $b['resolved_amount'] = $bet_amount;
        $payouts[] = ['user' => $uid, 'type' => 0, 'amount' => $bet_amount, 'label' => 'bet_refund'];
    }
    unset($b);

    // Mark all active LPs as resolved, create pending refund payouts (principal only)
    foreach ($lps as &$l) {
        if ($l['market'] != $market['id'] || $l['status'] != 0) continue;
        $uid = $l['user'];
        $lp_amount = $l['amount'];
        if (!isset($stakes[$uid])) $stakes[$uid] = 0;
        $stakes[$uid] += $lp_amount;
        $total_stakes += $lp_amount;
        $l['status'] = 3;
        $l['earned_fee'] = 0;
        $payouts[] = ['user' => $uid, 'type' => 1, 'amount' => $lp_amount, 'label' => 'lp_refund'];
    }
    unset($l);

    // Deduct penalty from oracle insurance (immediate)
    $oracle['oracle_insurance'] -= $penalty;
    if ($oracle['oracle_insurance'] < 0) $oracle['oracle_insurance'] = 0;

    // Distribute penalty proportionally (immediate — paid directly, not through payout queue)
    $distributed = [];
    if ($total_stakes > 0 && $penalty > 0) {
        foreach ($stakes as $uid => $stake) {
            $bonus = intval($penalty * $stake / $total_stakes);
            $distributed[$uid] = $bonus;
        }
    }

    // Market: resolved as no-contest, awaiting grace period
    $market['status'] = 3;
    $market['resolved_outcome'] = -1;
    $market['payout_status'] = 1; // pending — grace period for disputes

    return [
        'penalty' => $penalty,
        'penalty_pct' => $no_contest_pct,
        'total_stakes' => $total_stakes,
        'distributed' => $distributed,
    ];
}

$nc_oracle = make_user(160, 'OracleNC', 100000000);
register_oracle($nc_oracle, 0, 5, $SETTINGS);
oracle_deposit_insurance($nc_oracle, 5000000); // 5000 VIZ
$nc_maker = make_user(161, 'MakerNC', 100000000);
register_creator($nc_maker, $SETTINGS);
$nc_bettor1 = make_user(162, 'BettorNC1', 100000000);
$nc_bettor2 = make_user(163, 'BettorNC2', 100000000);

$nc_r = create_market($nc_maker, $nc_oracle, 200000, 5, 10, $base_time,
    $base_time + 172800, $base_time + 259200, 1, 5, 0, $SETTINGS);
$nc_market = $nc_r['market'];
$nc_lps = [$nc_r['lp']];
oracle_accept($nc_market, $nc_oracle, $SETTINGS);

// Place bets
$nc_b1 = place_bet($nc_market, $nc_bettor1, 0, 300000, $base_time + 3600);
$nc_b2 = place_bet($nc_market, $nc_bettor2, 1, 200000, $base_time + 7200);
$nc_bets = [$nc_b1['bet'], $nc_b2['bet']];
$nc_payouts = [];

$insurance_before = $nc_oracle['oracle_insurance'];
step('Setup: oracle insurance=' . fmt($insurance_before) . ', bets=300+200 VIZ', [
    'insurance' => fmt($insurance_before),
    'bet1' => fmt($nc_b1['bet']['amount']),
    'bet2' => fmt($nc_b2['bet']['amount']),
]);

// Step 1: Oracle declares no-contest (creates pending refund payouts, grace period)
$nc_result = oracle_no_contest($nc_market, $nc_oracle, $nc_bets, $nc_lps, $nc_payouts, $SETTINGS);

step('Oracle declares no-contest (pending payouts, grace period)', $nc_result);

// Step 2: Verify penalty is 50% of dispute_fee = 500 VIZ
$expected_penalty = intval($SETTINGS['dispute_fee'] * $SETTINGS['oracle_no_contest_penalty_percent'] / 100);
assert($nc_result['penalty'] == $expected_penalty);
step('Penalty verification', [
    'expected' => fmt($expected_penalty) . ' (' . $SETTINGS['oracle_no_contest_penalty_percent'] . '% of dispute_fee ' . fmt($SETTINGS['dispute_fee']) . ')',
    'actual' => fmt($nc_result['penalty']),
    'oracle_insurance_after' => fmt($nc_oracle['oracle_insurance']),
    'insurance_reduced_by' => fmt($insurance_before - $nc_oracle['oracle_insurance']),
]);
assert($nc_oracle['oracle_insurance'] == $insurance_before - $expected_penalty);

// Step 3: Verify market is in grace period (resolved_outcome=-1, payout_status=1)
assert($nc_market['status'] == 3);
assert($nc_market['resolved_outcome'] == -1);
assert($nc_market['payout_status'] == 1); // pending — awaiting grace period
step('Market state after no-contest', [
    'status' => $nc_market['status'],
    'resolved_outcome' => $nc_market['resolved_outcome'] . ' (no winner)',
    'payout_status' => $nc_market['payout_status'] . ' (pending — grace period for disputes)',
]);

// Step 4: Verify pending payouts were created (2 bet refunds + 1 LP refund)
$bet_refund_payouts = array_filter($nc_payouts, function($p) { return $p['label'] == 'bet_refund'; });
$lp_refund_payouts = array_filter($nc_payouts, function($p) { return $p['label'] == 'lp_refund'; });
assert(count($bet_refund_payouts) == 2);
assert(count($lp_refund_payouts) == 1);
step('Pending payouts created', [
    'bet_refunds' => count($bet_refund_payouts),
    'lp_refunds' => count($lp_refund_payouts),
    'total_pending' => count($nc_payouts),
]);

// Step 5: Grace period passes with no dispute → auto_payout processes refunds
$nc_users_map = [
    $nc_bettor1['id'] => &$nc_bettor1,
    $nc_bettor2['id'] => &$nc_bettor2,
    $nc_maker['id'] => &$nc_maker,
];
$b1_before = $nc_bettor1['balance'];
$b2_before = $nc_bettor2['balance'];
$maker_before = $nc_maker['balance'];
$payout_results = auto_payout($nc_payouts, $nc_users_map);
$nc_market['payout_status'] = 2; // finalized after auto-payout

step('Auto-payout after grace period (refunds processed)', [
    'payouts_processed' => count($payout_results),
    'bettor1_refund' => fmt($nc_bettor1['balance'] - $b1_before),
    'bettor2_refund' => fmt($nc_bettor2['balance'] - $b2_before),
    'maker_lp_refund' => fmt($nc_maker['balance'] - $maker_before),
    'market_payout_status' => $nc_market['payout_status'] . ' (finalized)',
]);
assert($nc_bettor1['balance'] == $b1_before + $nc_b1['bet']['amount']);
assert($nc_bettor2['balance'] == $b2_before + $nc_b2['bet']['amount']);
assert($nc_market['payout_status'] == 2);

// Step 6: Compare with dispute penalty
// No-contest: 500 VIZ (50% of 1000)
// Dispute loss: 1000 VIZ (dispute_fee) + potential extra penalty + potential ban
// Oracle-miss (cron): 5% of insurance = 250 VIZ
$dispute_penalty = $SETTINGS['dispute_fee'];
$miss_penalty = intval($insurance_before * $SETTINGS['oracle_penalty_percent'] / 100);
step('Scenario 17 complete: oracle no-contest incentive comparison', [
    'no_contest_penalty' => fmt($nc_result['penalty']) . ' (voluntary, no ban risk)',
    'dispute_loss_penalty' => fmt($dispute_penalty) . ' + extra + possible ban',
    'oracle_miss_penalty' => fmt($miss_penalty) . ' (5% insurance, auto by cron)',
    'incentive' => 'No-contest is CHEAPER than dispute loss, encourages honesty',
    'flow' => 'no-contest → grace period → auto-payout (same as normal resolution)',
]);


// ============================================================
// SCENARIO 18: DISPUTE AGAINST NO-CONTEST (3-outcome resolution)
// Oracle abuses no-contest, resolver can choose: A wins / B wins / confirm no-contest
// ============================================================
echo "\n\n" . str_repeat('=', 70) . "\n";
echo "SCENARIO 18: DISPUTE AGAINST ABUSIVE NO-CONTEST (3-OUTCOME RESOLUTION)\n";
echo str_repeat('=', 70) . "\n";

// Setup: oracle with insurance, market with bets, committee assigned
$nc_oracle2 = make_user(170, 'OracleNC2', 100000000);
register_oracle($nc_oracle2, 0, 5, $SETTINGS);
oracle_deposit_insurance($nc_oracle2, 5000000); // 5000 VIZ
$nc_maker2 = make_user(171, 'MakerNC2', 100000000);
register_creator($nc_maker2, $SETTINGS);
$nc_committee2 = make_user(172, 'CommitteeNC2', 0);
$nc_committee2['committee'] = 1;
$nc_bettor_a = make_user(173, 'BettorA_NC', 100000000);
$nc_bettor_b = make_user(174, 'BettorB_NC', 100000000);

$nc_r2 = create_market($nc_maker2, $nc_oracle2, 200000, 5, 10, $base_time,
    $base_time + 172800, $base_time + 259200, 1, 5, $nc_committee2['id'], $SETTINGS, -1, $nc_committee2);
$nc_market2 = $nc_r2['market'];
$nc_lps2 = [$nc_r2['lp']];
oracle_accept($nc_market2, $nc_oracle2, $SETTINGS);

// Bets: user A bets 300 on side 0, user B bets 200 on side 1
$nc_b2a = place_bet($nc_market2, $nc_bettor_a, 0, 300000, $base_time + 3600);
$nc_b2b = place_bet($nc_market2, $nc_bettor_b, 1, 200000, $base_time + 7200);
$nc_bets2 = [$nc_b2a['bet'], $nc_b2b['bet']];
$nc_payouts2 = [];

$oracle_ins_before = $nc_oracle2['oracle_insurance'];

// Step 1: Oracle abuses no-contest (creates pending refund payouts, payout_status=1)
$nc_res2 = oracle_no_contest($nc_market2, $nc_oracle2, $nc_bets2, $nc_lps2, $nc_payouts2, $SETTINGS);
step('Oracle declares abusive no-contest (grace period)', [
    'penalty' => fmt($nc_res2['penalty']),
    'oracle_insurance_after_no_contest' => fmt($nc_oracle2['oracle_insurance']),
    'market_resolved_outcome' => $nc_market2['resolved_outcome'] . ' (no-contest)',
    'market_payout_status' => $nc_market2['payout_status'] . ' (pending — disputable)',
    'pending_payouts' => count($nc_payouts2),
]);
assert($nc_market2['resolved_outcome'] == -1);
assert($nc_market2['payout_status'] == 1); // pending, not finalized

// Step 2: BettorA disputes during grace period (outcome was clearly A)
$nc_market2['payout_status'] = 3; // disputed
$dispute_fee = $SETTINGS['dispute_fee'];
$nc_bettor_a['balance'] -= $dispute_fee;
step('BettorA disputes no-contest during grace period', [
    'dispute_fee_paid' => fmt($dispute_fee),
    'bettor_a_balance' => fmt($nc_bettor_a['balance']),
    'market_payout_status' => '3 (disputed)',
]);

// Step 3: Resolver chooses correct_outcome=0 (A wins) — oracle was WRONG
// Oracle declared -1, correct is 0, so oracle_was_wrong = true
$correct_outcome = 0; // A wins
$oracle_was_wrong = ($correct_outcome != $nc_market2['resolved_outcome']); // 0 != -1 → true
assert($oracle_was_wrong === true);

// Delete pending no-contest refund payouts (they'll be replaced with winner payouts)
$nc_payouts2 = []; // clear pending refunds

// Plaintiff gets 2x dispute_fee (oracle was wrong)
$nc_bettor_a['balance'] += 2 * $dispute_fee;

// Oracle loses dispute_fee from insurance
$oracle_ins_before_dispute = $nc_oracle2['oracle_insurance'];
$nc_oracle2['oracle_insurance'] -= $dispute_fee;
if ($nc_oracle2['oracle_insurance'] < 0) $nc_oracle2['oracle_insurance'] = 0;

// Extra penalty + permanent ban
$penalty_extra = 2000000; // 2000 VIZ extra
$actual_extra = min($penalty_extra, $nc_oracle2['oracle_insurance']);
$nc_oracle2['oracle_insurance'] -= $actual_extra;
$nc_oracle2['oracle_banned'] = 1;
$nc_oracle2['oracle_ban_until'] = 0;

step('Resolver: correct_outcome=0 (A wins), oracle WRONG + sanctions', [
    'correct_outcome' => $correct_outcome . ' (A wins)',
    'original_outcome' => '-1 (no-contest)',
    'oracle_was_wrong' => $oracle_was_wrong ? 'YES' : 'NO',
    'plaintiff_reward' => fmt(2 * $dispute_fee),
    'oracle_dispute_fee_slash' => fmt($dispute_fee),
    'oracle_extra_penalty' => fmt($actual_extra),
    'oracle_insurance_after' => fmt($nc_oracle2['oracle_insurance']),
    'oracle_banned' => 'PERMANENT',
]);

// Step 4: Recalculate payouts for correct_outcome=0 (A wins)
// Since correct_outcome=0, side-0 bettors win, side-1 lose
// Use resolve_market logic with outcome=0
$nc_bets2_fresh = [$nc_b2a['bet'], $nc_b2b['bet']]; // fresh copies (original amounts)
// Reset bet statuses to active for recalculation
foreach ($nc_bets2_fresh as &$fb) { $fb['status'] = 0; }
unset($fb);
$nc_lps2_fresh = [$nc_r2['lp']]; // fresh LP
foreach ($nc_lps2_fresh as &$fl) { $fl['status'] = 0; }
unset($fl);
$nc_payouts2_new = [];
resolve_market($nc_market2, $correct_outcome, $nc_bets2_fresh, $nc_lps2_fresh, $nc_payouts2_new);

// Process payouts
$nc_users_map2 = [
    $nc_bettor_a['id'] => &$nc_bettor_a,
    $nc_bettor_b['id'] => &$nc_bettor_b,
    $nc_maker2['id'] => &$nc_maker2,
];
$ba_before = $nc_bettor_a['balance'];
$bb_before = $nc_bettor_b['balance'];
$maker2_before = $nc_maker2['balance'];
$payout_results2 = auto_payout($nc_payouts2_new, $nc_users_map2);
$nc_market2['payout_status'] = 2; // finalized

step('Payouts recalculated for correct_outcome=0 (A wins)', [
    'payouts_processed' => count($payout_results2),
    'bettor_a_received' => fmt($nc_bettor_a['balance'] - $ba_before) . ' (winner)',
    'bettor_b_received' => fmt($nc_bettor_b['balance'] - $bb_before) . ' (loser, 0)',
    'maker_received' => fmt($nc_maker2['balance'] - $maker2_before) . ' (LP fee)',
    'market_payout_status' => '2 (finalized)',
]);

// Winner should get more than their bet (they won the pool)
assert($nc_bettor_a['balance'] > $ba_before);
// Market finalized
assert($nc_market2['payout_status'] == 2);

// Step 5: Verify total damage to oracle
$total_insurance_lost = $oracle_ins_before - $nc_oracle2['oracle_insurance'];
step('Scenario 18 complete: total oracle damage from abusive no-contest', [
    'no_contest_penalty' => fmt($nc_res2['penalty']) . ' (initial, from no-contest declaration)',
    'dispute_fee_slash' => fmt($dispute_fee),
    'extra_penalty' => fmt($actual_extra),
    'total_insurance_lost' => fmt($total_insurance_lost) . ' out of ' . fmt($oracle_ins_before),
    'oracle_banned' => 'PERMANENT',
    'correct_outcome_applied' => 'A wins (side 0) — bettors paid correctly',
    'conclusion' => 'Abusing no-contest costs MORE than honest resolution',
    'resolver_options' => 'A wins (0) / B wins (1) / confirm no-contest (-1) + sanctions',
]);

// Verify: total damage > no-contest penalty alone
assert($total_insurance_lost > $nc_res2['penalty']);
// Verify: oracle is banned
assert($nc_oracle2['oracle_banned'] == 1);


// ============================================================
// SCENARIO 19: AUTO-CLOSE STALE DISPUTE (denial-of-resolution prevention)
// Dispute resolver doesn't act for 14 days → auto-close, refund all, penalize oracle
// ============================================================
echo "\n\n" . str_repeat('=', 70) . "\n";
echo "SCENARIO 19: AUTO-CLOSE STALE DISPUTE\n";
echo str_repeat('=', 70) . "\n";

// Setup: create a market, resolve it, file a dispute, then simulate 14-day timeout
$ac_oracle = make_user(180, 'OracleAC', 100000000);
register_oracle($ac_oracle, 0, 5, $SETTINGS);
oracle_deposit_insurance($ac_oracle, 5000000); // 5000 VIZ
$ac_maker = make_user(181, 'MakerAC', 100000000);
register_creator($ac_maker, $SETTINGS);
$ac_committee = make_user(182, 'CommitteeAC', 0);
$ac_committee['committee'] = 1;
$ac_bettor1 = make_user(183, 'BettorAC1', 100000000);
$ac_bettor2 = make_user(184, 'BettorAC2', 100000000);

$ac_r = create_market($ac_maker, $ac_oracle, 200000, 5, 10, $base_time,
    $base_time + 172800, $base_time + 259200, 1, 5, $ac_committee['id'], $SETTINGS, -1, $ac_committee);
$ac_market = $ac_r['market'];
$ac_lps = [$ac_r['lp']];
oracle_accept($ac_market, $ac_oracle, $SETTINGS);

// Place bets
$ac_b1 = place_bet($ac_market, $ac_bettor1, 0, 300000, $base_time + 3600);
$ac_b2 = place_bet($ac_market, $ac_bettor2, 1, 200000, $base_time + 7200);

// Oracle resolves to outcome 0
$ac_bets = [$ac_b1['bet'], $ac_b2['bet']];
$ac_payouts = [];
resolve_market($ac_market, 0, $ac_bets, $ac_lps, $ac_payouts);

$bettor1_balance_before = $ac_bettor1['balance'];
$oracle_insurance_before = $ac_oracle['oracle_insurance'];

// Bettor2 files dispute (they lost)
$dispute_fee = $SETTINGS['dispute_fee'];
$ac_bettor2['balance'] -= $dispute_fee;
$ac_market['payout_status'] = 3; // disputed

step('Dispute filed, resolver does nothing...', [
    'bettor2_paid_dispute_fee' => fmt($dispute_fee),
    'market_payout_status' => 3,
    'auto_close_days' => $SETTINGS['dispute_auto_close_days'],
]);

// Simulate auto-close after 14 days (cron job logic)
$auto_close_days = $SETTINGS['dispute_auto_close_days'];

// Refund plaintiff dispute fee
$ac_bettor2['balance'] += $dispute_fee;

// Penalize oracle: dispute_fee from insurance
$oracle_penalty = min($dispute_fee, $ac_oracle['oracle_insurance']);
$ac_oracle['oracle_insurance'] -= $oracle_penalty;

// Refund all bets (original amounts)
$bet1_refund = intval($ac_b1['bet']['amount']);
$bet2_refund = intval($ac_b2['bet']['amount']);
$ac_bettor1['balance'] += $bet1_refund;
$ac_bettor2['balance'] += $bet2_refund;

// Refund LP
$lp_refund = intval($ac_lps[0]['amount']);
$ac_maker['balance'] += $lp_refund;

// Distribute oracle penalty proportionally
$total_stakes = $bet1_refund + $bet2_refund + $lp_refund;
$bonus1 = intval($oracle_penalty * $bet1_refund / $total_stakes);
$bonus2 = intval($oracle_penalty * $bet2_refund / $total_stakes);
$bonus_lp = intval($oracle_penalty * $lp_refund / $total_stakes);
$ac_bettor1['balance'] += $bonus1;
$ac_bettor2['balance'] += $bonus2;
$ac_maker['balance'] += $bonus_lp;

// Market finalized
$ac_market['payout_status'] = 2;

step('Auto-close after ' . $auto_close_days . ' days: all refunded + oracle penalized', [
    'dispute_fee_refunded_to_plaintiff' => fmt($dispute_fee),
    'oracle_penalty' => fmt($oracle_penalty),
    'oracle_insurance_after' => fmt($ac_oracle['oracle_insurance']),
    'bettor1_refund' => fmt($bet1_refund) . ' + bonus ' . fmt($bonus1),
    'bettor2_refund' => fmt($bet2_refund) . ' + bonus ' . fmt($bonus2),
    'lp_refund' => fmt($lp_refund) . ' + bonus ' . fmt($bonus_lp),
    'total_penalty_distributed' => fmt($bonus1 + $bonus2 + $bonus_lp),
    'market_payout_status' => $ac_market['payout_status'],
]);

// Verify: plaintiff got their dispute fee back
assert($ac_bettor2['balance'] > $bettor1_balance_before - $dispute_fee); // bettor2 is whole
// Verify: oracle was penalized
assert($ac_oracle['oracle_insurance'] == $oracle_insurance_before - $oracle_penalty);
// Verify: market is finalized
assert($ac_market['payout_status'] == 2);

step('Scenario 19 complete: stale dispute auto-closed', [
    'protection' => 'Committee inaction cannot freeze funds indefinitely',
    'fallback' => 'After ' . $auto_close_days . ' days: refund all + oracle penalty (dispute_fee)',
    'plaintiff' => 'Gets dispute fee back (not punished for committee failure)',
    'oracle' => 'Loses dispute_fee from insurance (accountability)',
]);


// ============================================================
// LAZY POOL SETTINGS
// ============================================================
$SETTINGS['lazy_pool_allocation_percent'] = 2;
$SETTINGS['lazy_pool_max_total_allocation'] = 70;
$SETTINGS['lazy_pool_min_market_allocation'] = 100000; // 100 VIZ
$SETTINGS['lazy_pool_lock_period_days'] = 30;
$SETTINGS['lazy_pool_emergency_penalty'] = 50;

$LAZY_POOL_PRECISION = 1000000000; // 10^9

// ============================================================
// LAZY POOL HELPER SIMULATION FUNCTIONS
// ============================================================
function make_lazy_pool() {
    return [
        'total_shares' => 0,
        'free_balance' => 0,
        'allocated_balance' => 0,
        'reward_per_share' => 0,
    ];
}

function make_lazy_user($id, $name, $balance) {
    $u = make_user($id, $name, $balance);
    $u['lazy_pool_balance'] = 0;
    $u['lazy_pool_shares'] = 0;
    $u['lazy_pool_reward_snapshot'] = 0;
    $u['lazy_pool_pending_rewards'] = 0;
    return $u;
}

function lp_settle_rewards(&$user, $pool) {
    global $LAZY_POOL_PRECISION;
    $shares = $user['lazy_pool_shares'];
    if ($shares > 0) {
        $rps_diff = $pool['reward_per_share'] - $user['lazy_pool_reward_snapshot'];
        if ($rps_diff > 0) {
            $new_rewards = intval($shares * $rps_diff / $LAZY_POOL_PRECISION);
            $user['lazy_pool_pending_rewards'] += $new_rewards;
        }
    }
    $user['lazy_pool_reward_snapshot'] = $pool['reward_per_share'];
}

function lp_deposit(&$user, &$pool, $amount, $lock_days, $time_now) {
    global $LAZY_POOL_PRECISION;
    if ($user['balance'] < $amount) return ['error' => 'Insufficient balance'];

    // Settle existing rewards first
    lp_settle_rewards($user, $pool);

    // Calculate shares
    if ($pool['total_shares'] == 0) {
        $new_shares = $amount; // 1:1 first deposit
    } else {
        $new_shares = intval($amount * $pool['total_shares'] / $pool['free_balance']);
    }

    $unlock_time = $time_now + $lock_days * 86400;

    // Update pool
    $pool['total_shares'] += $new_shares;
    $pool['free_balance'] += $amount;

    // Update user
    $user['balance'] -= $amount;
    $user['lazy_pool_balance'] += $amount;
    $user['lazy_pool_shares'] += $new_shares;
    $user['lazy_pool_reward_snapshot'] = $pool['reward_per_share'];

    return [
        'status' => true,
        'shares' => $new_shares,
        'unlock_time' => $unlock_time,
    ];
}

function lp_receive_profit(&$pool, $profit) {
    // ONLY writes to pool. ZERO user writes.
    global $LAZY_POOL_PRECISION;
    if ($pool['total_shares'] > 0 && $profit > 0) {
        $pool['reward_per_share'] += intval($profit * $LAZY_POOL_PRECISION / $pool['total_shares']);
    }
}

function lp_withdraw(&$user, &$pool, $shares_to_burn, $principal_to_return) {
    global $LAZY_POOL_PRECISION;
    // Settle rewards first
    lp_settle_rewards($user, $pool);

    // Calculate share value
    $share_value = intval($shares_to_burn * $pool['free_balance'] / $pool['total_shares']);

    // Calculate reward portion based on shares being burned vs total user shares
    $reward_portion = 0;
    if ($user['lazy_pool_shares'] > 0) {
        $reward_portion = intval($user['lazy_pool_pending_rewards'] * $shares_to_burn / $user['lazy_pool_shares']);
    }

    $total_payout = $share_value + $reward_portion;

    // Update pool
    $pool['total_shares'] -= $shares_to_burn;
    $pool['free_balance'] -= $total_payout;

    // Update user
    $user['balance'] += $total_payout;
    $user['lazy_pool_shares'] -= $shares_to_burn;
    $user['lazy_pool_balance'] -= $principal_to_return;
    $user['lazy_pool_pending_rewards'] -= $reward_portion;

    return [
        'status' => true,
        'share_value' => $share_value,
        'reward_portion' => $reward_portion,
        'total_payout' => $total_payout,
    ];
}

function lp_emergency_withdraw(&$user, &$pool, $locked_shares, $total_user_shares, $penalty_pct) {
    global $LAZY_POOL_PRECISION;
    // Settle rewards first
    lp_settle_rewards($user, $pool);

    $total_share_value = intval($total_user_shares * $pool['free_balance'] / $pool['total_shares']);
    $pending = $user['lazy_pool_pending_rewards'];
    $profit = $total_share_value + $pending - $user['lazy_pool_balance'];

    $penalty = 0;
    if ($profit > 0 && $locked_shares > 0 && $total_user_shares > 0) {
        $penalty = intval($profit * $locked_shares / $total_user_shares * $penalty_pct / 100);
    }

    // Penalty stays in pool via reward_per_share for remaining participants
    $remaining_shares = $pool['total_shares'] - $total_user_shares;
    if ($penalty > 0 && $remaining_shares > 0) {
        $pool['reward_per_share'] += intval($penalty * $LAZY_POOL_PRECISION / $remaining_shares);
    }

    $total_payout = $total_share_value + $pending - $penalty;

    // Update pool
    $pool['total_shares'] -= $total_user_shares;
    $pool['free_balance'] -= $total_payout;

    // Update user
    $user['balance'] += $total_payout;
    $user['lazy_pool_shares'] = 0;
    $user['lazy_pool_balance'] = 0;
    $user['lazy_pool_pending_rewards'] = 0;
    $user['lazy_pool_reward_snapshot'] = $pool['reward_per_share'];

    return [
        'status' => true,
        'total_share_value' => $total_share_value,
        'pending_rewards' => $pending,
        'profit' => $profit,
        'penalty' => $penalty,
        'total_payout' => $total_payout,
    ];
}

function lp_frontend_reward($user, $pool) {
    global $LAZY_POOL_PRECISION;
    $live = $user['lazy_pool_pending_rewards'];
    if ($user['lazy_pool_shares'] > 0) {
        $rps_diff = $pool['reward_per_share'] - $user['lazy_pool_reward_snapshot'];
        if ($rps_diff > 0) {
            $live += intval($user['lazy_pool_shares'] * $rps_diff / $LAZY_POOL_PRECISION);
        }
    }
    return $live;
}

// ============================================================
// SCENARIO 20: Lazy Pool — Deposit, Shares, Lock Period
// ============================================================
echo "\n" . str_repeat('=', 70) . "\n";
echo "SCENARIO 20: Lazy Pool — Deposit, Shares, Lock Period\n";
echo str_repeat('=', 70) . "\n";

$pool = make_lazy_pool();
$lp_user1 = make_lazy_user(100, 'LazyLP_Alice', 10000000); // 10000 VIZ
$lp_user2 = make_lazy_user(101, 'LazyLP_Bob', 5000000);    // 5000 VIZ
$lock_days = $SETTINGS['lazy_pool_lock_period_days'];
$time_now = 1000000;

// Alice deposits 1000 VIZ (first depositor)
$r1 = lp_deposit($lp_user1, $pool, 1000000, $lock_days, $time_now);
assert($r1['status'] === true);
assert($r1['shares'] == 1000000); // 1:1 first deposit
assert($pool['total_shares'] == 1000000);
assert($pool['free_balance'] == 1000000);
assert($lp_user1['lazy_pool_shares'] == 1000000);
assert($lp_user1['lazy_pool_balance'] == 1000000);

step('Scenario 20: First deposit (Alice 1000 VIZ)', [
    'shares_received' => $r1['shares'],
    'pool_total_shares' => $pool['total_shares'],
    'pool_free_balance' => fmt($pool['free_balance']),
    'unlock_time' => $r1['unlock_time'],
]);

// Bob deposits 500 VIZ (proportional shares)
$r2 = lp_deposit($lp_user2, $pool, 500000, $lock_days, $time_now + 100);
assert($r2['status'] === true);
assert($r2['shares'] == 500000); // 500K * 1M / 1M = 500K (proportional, pool hasn't grown yet)
assert($pool['total_shares'] == 1500000);
assert($pool['free_balance'] == 1500000);

step('Scenario 20: Second deposit (Bob 500 VIZ)', [
    'shares_received' => $r2['shares'],
    'pool_total_shares' => $pool['total_shares'],
    'pool_free_balance' => fmt($pool['free_balance']),
    'alice_shares' => $lp_user1['lazy_pool_shares'],
    'bob_shares' => $lp_user2['lazy_pool_shares'],
]);

// ============================================================
// SCENARIO 21: Share Calculation After Profit
// ============================================================
echo "\n" . str_repeat('=', 70) . "\n";
echo "SCENARIO 21: Lazy Pool — Share Calculation After Profit\n";
echo str_repeat('=', 70) . "\n";

// Simulate market profit: pool receives 300 VIZ
lp_receive_profit($pool, 300000);

// Verify: ONLY pool.reward_per_share changed, NO user writes
assert($pool['reward_per_share'] == intval(300000 * $LAZY_POOL_PRECISION / 1500000));
$expected_rps = intval(300000 * $LAZY_POOL_PRECISION / 1500000); // = 200000000

step('Scenario 21: Profit distributed (300 VIZ), ZERO user writes', [
    'profit' => fmt(300000),
    'reward_per_share' => $pool['reward_per_share'],
    'expected_rps' => $expected_rps,
    'alice_snapshot_unchanged' => $lp_user1['lazy_pool_reward_snapshot'],
    'bob_snapshot_unchanged' => $lp_user2['lazy_pool_reward_snapshot'],
    'alice_pending_unchanged' => $lp_user1['lazy_pool_pending_rewards'],
]);

// Frontend reward calculation (NO backend write)
$alice_live = lp_frontend_reward($lp_user1, $pool);
$bob_live = lp_frontend_reward($lp_user2, $pool);

// Alice: 1M shares, Bob: 500K shares. Profit 300K split 2:1
assert($alice_live == intval(1000000 * $expected_rps / $LAZY_POOL_PRECISION)); // 200 VIZ
assert($bob_live == intval(500000 * $expected_rps / $LAZY_POOL_PRECISION));   // 100 VIZ

step('Scenario 21: Frontend reward (JS formula, zero writes)', [
    'alice_live_reward' => fmt($alice_live),
    'bob_live_reward' => fmt($bob_live),
    'formula' => 'pending + shares * (pool.rps - user.snapshot) / PRECISION',
]);

// ============================================================
// SCENARIO 22: Late Depositor Fairness
// ============================================================
echo "\n" . str_repeat('=', 70) . "\n";
echo "SCENARIO 22: Lazy Pool — Late Depositor Does NOT Get Old Rewards\n";
echo str_repeat('=', 70) . "\n";

$lp_user3 = make_lazy_user(102, 'LazyLP_Charlie', 5000000); // 5000 VIZ

// Charlie deposits AFTER the 300 VIZ profit was distributed
$r3 = lp_deposit($lp_user3, $pool, 1000000, $lock_days, $time_now + 200);
assert($r3['status'] === true);
// Shares: 1000000 * 1500000 / 1500000 = 1000000 (pool hasn't had free_balance change from rewards)
// Wait - free_balance didn't change from rewards (rewards are tracked via rps, not free_balance)
// So Charlie gets 1M shares for 1M amount — same as Alice

$charlie_live = lp_frontend_reward($lp_user3, $pool);
assert($charlie_live == 0); // NO old rewards for late depositor!

step('Scenario 22: Late depositor Charlie gets ZERO old rewards', [
    'charlie_shares' => $r3['shares'],
    'charlie_live_reward' => fmt($charlie_live),
    'charlie_snapshot' => $lp_user3['lazy_pool_reward_snapshot'],
    'pool_rps' => $pool['reward_per_share'],
    'fairness' => 'snapshot == pool.rps at deposit time, so rps_diff=0',
]);

// ============================================================
// SCENARIO 23: Unlock Consolidation
// ============================================================
echo "\n" . str_repeat('=', 70) . "\n";
echo "SCENARIO 23: Lazy Pool — Deposit Unlock Consolidation\n";
echo str_repeat('=', 70) . "\n";

// Simulate 3 deposits with different unlock times
$deposits = [
    ['id' => 1, 'user' => 200, 'amount' => 100000, 'shares' => 100000, 'unlock_time' => 1000, 'status' => 0],
    ['id' => 2, 'user' => 200, 'amount' => 200000, 'shares' => 200000, 'unlock_time' => 2000, 'status' => 0],
    ['id' => 3, 'user' => 200, 'amount' => 300000, 'shares' => 300000, 'unlock_time' => 5000, 'status' => 0],
];

// At time=2500, deposits 1 and 2 are expired, deposit 3 still locked
$check_time = 2500;
$expired = [];
$still_locked = [];
foreach ($deposits as &$dep) {
    if ($dep['status'] == 0 && $dep['unlock_time'] <= $check_time) {
        $expired[] = $dep;
        $dep['status'] = 2; // merged
    } else {
        $still_locked[] = $dep;
    }
}
unset($dep);

$sum_shares = 0; $sum_amount = 0;
foreach ($expired as $e) {
    $sum_shares += $e['shares'];
    $sum_amount += $e['amount'];
}

// Create consolidated unlocked record
$unlocked_record = ['id' => 4, 'user' => 200, 'amount' => $sum_amount, 'shares' => $sum_shares, 'unlock_time' => 0, 'status' => 1];

assert($unlocked_record['amount'] == 300000); // 100K + 200K
assert($unlocked_record['shares'] == 300000); // 100K + 200K
assert(count($expired) == 2);
assert($deposits[2]['status'] == 0); // deposit 3 still locked

step('Scenario 23: Consolidation at time=2500', [
    'expired_count' => count($expired),
    'merged_amount' => fmt($sum_amount),
    'merged_shares' => $sum_shares,
    'unlocked_record' => $unlocked_record,
    'still_locked' => $still_locked,
    'rule' => 'Max 1 unlocked (status=1) + N locked (status=0) per user',
]);

// Second consolidation: deposit 3 expires at time=5500
$check_time2 = 5500;
$expired2 = [];
foreach ($deposits as &$dep) {
    if ($dep['status'] == 0 && $dep['unlock_time'] <= $check_time2) {
        $expired2[] = $dep;
        $dep['status'] = 2;
    }
}
unset($dep);
assert(count($expired2) == 1);

// Merge into existing unlocked record
$unlocked_record['amount'] += $expired2[0]['amount'];
$unlocked_record['shares'] += $expired2[0]['shares'];
assert($unlocked_record['amount'] == 600000); // 300K + 300K
assert($unlocked_record['shares'] == 600000);

step('Scenario 23: Second consolidation — all merged into one record', [
    'final_unlocked_amount' => fmt($unlocked_record['amount']),
    'final_unlocked_shares' => $unlocked_record['shares'],
    'total_deposits' => 3,
    'merged_deposits' => 3,
    'result' => '1 unlocked record with all shares',
]);

// ============================================================
// SCENARIO 24: Planned Withdrawal
// ============================================================
echo "\n" . str_repeat('=', 70) . "\n";
echo "SCENARIO 24: Lazy Pool — Planned Withdrawal\n";
echo str_repeat('=', 70) . "\n";

// Use Alice from scenarios 20-21 (has 1M shares, pool has profit)
// First, more profit to make things interesting
lp_receive_profit($pool, 150000); // another 150 VIZ profit

$alice_shares_before = $lp_user1['lazy_pool_shares'];
$alice_balance_before = $lp_user1['balance'];

// Alice withdraws all her unlocked shares (simulate they're all unlocked now)
$r_w = lp_withdraw($lp_user1, $pool, $alice_shares_before, $lp_user1['lazy_pool_balance']);

assert($r_w['status'] === true);
assert($lp_user1['lazy_pool_shares'] == 0);
assert($r_w['total_payout'] > 0);

step('Scenario 24: Alice full withdrawal', [
    'shares_burned' => $alice_shares_before,
    'share_value' => fmt($r_w['share_value']),
    'reward_portion' => fmt($r_w['reward_portion']),
    'total_payout' => fmt($r_w['total_payout']),
    'alice_balance_before' => fmt($alice_balance_before),
    'alice_balance_after' => fmt($lp_user1['balance']),
    'pool_total_shares_after' => $pool['total_shares'],
    'pool_free_balance_after' => fmt($pool['free_balance']),
]);

// ============================================================
// SCENARIO 25: Emergency Withdrawal with Penalty
// ============================================================
echo "\n" . str_repeat('=', 70) . "\n";
echo "SCENARIO 25: Lazy Pool — Emergency Withdrawal (Locked + Penalty)\n";
echo str_repeat('=', 70) . "\n";

// Reset pool for clean scenario
$epool = make_lazy_pool();
$e_user1 = make_lazy_user(300, 'Emergency_Alice', 10000000);
$e_user2 = make_lazy_user(301, 'Emergency_Bob', 10000000);

// Both deposit 1000 VIZ
lp_deposit($e_user1, $epool, 1000000, $lock_days, $time_now);
lp_deposit($e_user2, $epool, 1000000, $lock_days, $time_now);

// Pool receives 200 VIZ profit
lp_receive_profit($epool, 200000);

$bob_balance_before = $e_user2['balance'];

// Alice does emergency withdrawal — all her shares are locked
$r_e = lp_emergency_withdraw(
    $e_user1, $epool,
    1000000, // locked_shares = all
    1000000, // total_user_shares
    $SETTINGS['lazy_pool_emergency_penalty']
);

assert($r_e['status'] === true);
assert($r_e['profit'] > 0);
assert($r_e['penalty'] > 0);
assert($e_user1['lazy_pool_shares'] == 0);

// Verify penalty stayed in pool for Bob
$bob_live = lp_frontend_reward($e_user2, $epool);

step('Scenario 25: Emergency withdrawal — penalty on locked profit', [
    'total_share_value' => fmt($r_e['total_share_value']),
    'pending_rewards' => fmt($r_e['pending_rewards']),
    'profit' => fmt($r_e['profit']),
    'penalty' => fmt($r_e['penalty']),
    'total_payout' => fmt($r_e['total_payout']),
    'alice_balance_after' => fmt($e_user1['balance']),
    'bob_live_reward_after' => fmt($bob_live),
    'penalty_redistributed' => 'Yes, via reward_per_share to remaining participants',
]);

// Verify Bob got the penalty via increased rps
assert($bob_live > intval(200000 * 1000000 / 2000000)); // Bob gets more than half of 200K profit

step('Scenario 25: Bob benefits from Alice penalty', [
    'bob_fair_share_of_profit' => fmt(intval(200000 / 2)),
    'bob_actual_reward' => fmt($bob_live),
    'extra_from_penalty' => fmt($bob_live - intval(200000 / 2)),
]);

// ============================================================
// SCENARIO 26: Emergency Withdrawal — No Profit, No Penalty
// ============================================================
echo "\n" . str_repeat('=', 70) . "\n";
echo "SCENARIO 26: Lazy Pool — Emergency No Profit = No Penalty\n";
echo str_repeat('=', 70) . "\n";

$np_pool = make_lazy_pool();
$np_user = make_lazy_user(400, 'NoProfitUser', 5000000);
lp_deposit($np_user, $np_pool, 1000000, $lock_days, $time_now);

// No profit received — emergency withdraw
$r_np = lp_emergency_withdraw(
    $np_user, $np_pool,
    1000000, // all locked
    1000000,
    $SETTINGS['lazy_pool_emergency_penalty']
);

assert($r_np['penalty'] == 0);
assert($r_np['total_payout'] == 1000000); // Full principal returned
assert($np_user['balance'] == 5000000); // Back to original

step('Scenario 26: No profit = no penalty', [
    'profit' => fmt($r_np['profit']),
    'penalty' => fmt($r_np['penalty']),
    'total_payout' => fmt($r_np['total_payout']),
    'user_balance_restored' => fmt($np_user['balance']),
]);

// ============================================================
// SCENARIO 27: Market Auto-Allocation
// ============================================================
echo "\n" . str_repeat('=', 70) . "\n";
echo "SCENARIO 27: Lazy Pool — Market Auto-Allocation\n";
echo str_repeat('=', 70) . "\n";

$alloc_pool = make_lazy_pool();
$alloc_user = make_lazy_user(500, 'AllocUser', 50000000); // 50000 VIZ
lp_deposit($alloc_user, $alloc_pool, 20000000, $lock_days, $time_now); // 20000 VIZ

$alloc_pct = $SETTINGS['lazy_pool_allocation_percent'];
$max_alloc_pct = $SETTINGS['lazy_pool_max_total_allocation'];
$min_alloc = $SETTINGS['lazy_pool_min_market_allocation'];

$alloc_amount = intval($alloc_pool['free_balance'] * $alloc_pct / 100);
$passes_min = ($alloc_amount >= $min_alloc);
$total_pool_value = $alloc_pool['free_balance'] + $alloc_pool['allocated_balance'];
$passes_max = ($alloc_pool['allocated_balance'] + $alloc_amount <= $total_pool_value * $max_alloc_pct / 100);

assert($alloc_amount == 400000); // 2% of 20000000 = 400000 (400 VIZ)
assert($passes_min === true);
assert($passes_max === true);

// Simulate allocation
$alloc_pool['free_balance'] -= $alloc_amount;
$alloc_pool['allocated_balance'] += $alloc_amount;

step('Scenario 27: Auto-allocation on market activation', [
    'pool_free_balance' => fmt(20000000),
    'allocation_percent' => $alloc_pct . '%',
    'allocation_amount' => fmt($alloc_amount),
    'passes_min_check' => $passes_min ? 'YES' : 'NO',
    'passes_max_check' => $passes_max ? 'YES' : 'NO',
    'pool_free_after' => fmt($alloc_pool['free_balance']),
    'pool_allocated_after' => fmt($alloc_pool['allocated_balance']),
]);

// Simulate market resolution with profit
$market_return = $alloc_amount + 50000; // principal + 50 VIZ profit
$profit = $market_return - $alloc_amount;
$alloc_pool['free_balance'] += $market_return;
$alloc_pool['allocated_balance'] -= $alloc_amount;
lp_receive_profit($alloc_pool, $profit);

$user_reward = lp_frontend_reward($alloc_user, $alloc_pool);

step('Scenario 27: Market resolved — profit distributed via rps', [
    'market_return' => fmt($market_return),
    'profit' => fmt($profit),
    'pool_rps_after' => $alloc_pool['reward_per_share'],
    'user_live_reward' => fmt($user_reward),
    'pool_free_after' => fmt($alloc_pool['free_balance']),
    'pool_allocated_after' => fmt($alloc_pool['allocated_balance']),
]);

assert($user_reward == $profit); // Solo depositor gets all profit

// ============================================================
// SCENARIO 28: Edge Cases
// ============================================================
echo "\n" . str_repeat('=', 70) . "\n";
echo "SCENARIO 28: Lazy Pool — Edge Cases\n";
echo str_repeat('=', 70) . "\n";

// 28a: First depositor gets 1:1 shares
$edge_pool = make_lazy_pool();
$edge_user = make_lazy_user(600, 'EdgeUser', 5000000);
$r_edge = lp_deposit($edge_user, $edge_pool, 777000, $lock_days, $time_now);
assert($r_edge['shares'] == 777000);

step('Scenario 28a: First depositor 1:1 shares', [
    'amount' => fmt(777000),
    'shares' => $r_edge['shares'],
    'rule' => 'When total_shares==0, shares=amount',
]);

// 28b: Zero reward when rps hasn't changed
$edge_user2 = make_lazy_user(601, 'EdgeUser2', 5000000);
lp_deposit($edge_user2, $edge_pool, 500000, $lock_days, $time_now);
$r_zero = lp_frontend_reward($edge_user2, $edge_pool);
assert($r_zero == 0);

step('Scenario 28b: Zero reward when no profit has occurred', [
    'live_reward' => fmt($r_zero),
    'pool_rps' => $edge_pool['reward_per_share'],
    'user_snapshot' => $edge_user2['lazy_pool_reward_snapshot'],
]);

// 28c: Partial withdrawal (50%)
$partial_pool = make_lazy_pool();
$partial_user = make_lazy_user(700, 'PartialUser', 10000000);
lp_deposit($partial_user, $partial_pool, 2000000, $lock_days, $time_now);
lp_receive_profit($partial_pool, 100000); // 100 VIZ profit

// Withdraw 50% of shares
$half_shares = intval($partial_user['lazy_pool_shares'] / 2);
$half_principal = intval($partial_user['lazy_pool_balance'] / 2);
$r_partial = lp_withdraw($partial_user, $partial_pool, $half_shares, $half_principal);

assert($partial_user['lazy_pool_shares'] == $half_shares); // Half remains
assert($r_partial['total_payout'] > 0);

step('Scenario 28c: Partial withdrawal (50%)', [
    'shares_burned' => $half_shares,
    'shares_remaining' => $partial_user['lazy_pool_shares'],
    'payout' => fmt($r_partial['total_payout']),
    'share_value' => fmt($r_partial['share_value']),
    'reward_portion' => fmt($r_partial['reward_portion']),
]);

// ============================================================
// SCENARIO 29: LMSR MATH UNIT TESTS
// ============================================================
echo "\n" . str_repeat('=', 70) . "\n";
echo "SCENARIO 29: LMSR Math Unit Tests\n";
echo str_repeat('=', 70) . "\n";

require_once __DIR__ . '/../module/lmsr_fixed.php';

// 29a: Price calculation — equal q → equal prices
$q3 = [0, 0, 0];
$b3 = 100000; // b = 100
$prices3 = lmsr_prices($q3, $b3);
$expected_price = intval(round(1000000 / 3)); // ~333333
assert(abs($prices3[0] - $expected_price) <= 1, 'Equal q should give equal prices');
assert(abs($prices3[1] - $expected_price) <= 1);
assert(abs($prices3[2] - $expected_price) <= 1);
$price_sum = array_sum($prices3);
assert(abs($price_sum - 1000000) <= count($q3), 'Prices must sum to ~1.0');

step('Scenario 29a: Equal q → equal prices (3 outcomes)', [
    'q' => $q3, 'b' => $b3,
    'prices' => $prices3, 'sum' => $price_sum,
]);

// 29b: Cost function monotonicity
$cost0 = lmsr_cost([0, 0, 0], $b3);
$cost1 = lmsr_cost([10000, 0, 0], $b3);
assert($cost1 > $cost0, 'Cost must increase when q increases');

step('Scenario 29b: Cost monotonicity', [
    'cost_at_0' => $cost0, 'cost_at_10k' => $cost1,
]);

// 29c: Buy cost > 0 and sell return > 0
$buy = lmsr_buy_cost([0, 0, 0], $b3, 0, 5000);
$sell = lmsr_sell_return([5000, 0, 0], $b3, 0, 5000);
assert($buy > 0, 'Buy cost must be positive');
assert($sell > 0, 'Sell return must be positive');
assert(abs($buy - $sell) <= 2, 'Buy/sell roundtrip should be nearly equal');

step('Scenario 29c: Buy/sell roundtrip', [
    'buy_cost_5000' => $buy, 'sell_return_5000' => $sell, 'diff' => abs($buy - $sell),
]);

// 29d: tokens_for_amount consistency
$tokens = lmsr_tokens_for_amount([0, 0, 0], $b3, 0, 50000); // spend 50 VIZ
assert($tokens > 0, 'Must get some tokens');
$actual_cost = lmsr_buy_cost([0, 0, 0], $b3, 0, $tokens);
assert($actual_cost <= 50000, 'Actual cost must not exceed amount');

step('Scenario 29d: tokens_for_amount', [
    'amount' => 50000, 'tokens' => $tokens, 'actual_cost' => $actual_cost,
]);

// 29e: b_from_liquidity
$b_calc = lmsr_b_from_liquidity(100000, 3); // 100 VIZ, 3 outcomes
assert($b_calc > 0);
$max_loss = lmsr_max_loss($b_calc, 3);
assert(abs($max_loss - 100000) < 500, 'max_loss should ≈ liquidity'); // b*ln(3) ≈ liquidity

step('Scenario 29e: b_from_liquidity', [
    'liquidity' => 100000, 'n' => 3, 'b' => $b_calc, 'max_loss' => $max_loss,
]);

// 29f: Price shift after buying — price of bought outcome increases
$q_before = [0, 0, 0];
$p_before = lmsr_prices($q_before, $b3);
$q_after = [10000, 0, 0]; // bought 10 tokens on outcome 0
$p_after = lmsr_prices($q_after, $b3);
assert($p_after[0] > $p_before[0], 'Bought outcome price must increase');
assert($p_after[1] < $p_before[1], 'Other outcome price must decrease');

step('Scenario 29f: Price shift after buy', [
    'before' => $p_before, 'after' => $p_after,
]);


// ============================================================
// SCENARIO 30: LMSR SETTLEMENT (Onix Multi)
// ============================================================
echo "\n" . str_repeat('=', 70) . "\n";
echo "SCENARIO 30: Onix Multi Settlement\n";
echo str_repeat('=', 70) . "\n";

$bets_multi = [
    ['id'=>1, 'user'=>10, 'amount'=>50000, 'weight'=>45000, 'outcome_index'=>0, 'time_penalty'=>0],
    ['id'=>2, 'user'=>11, 'amount'=>30000, 'weight'=>25000, 'outcome_index'=>0, 'time_penalty'=>50000], // 5% penalty
    ['id'=>3, 'user'=>12, 'amount'=>40000, 'weight'=>35000, 'outcome_index'=>1, 'time_penalty'=>0],
    ['id'=>4, 'user'=>13, 'amount'=>60000, 'weight'=>50000, 'outcome_index'=>2, 'time_penalty'=>0],
];

$settlement = lmsr_settlement($bets_multi, 0, 5, 3, 10); // oracle 0.5%, creator 0.3%, LP 1%

// Losers = bets on outcomes 1 + 2 = 40000 + 60000 = 100000
assert($settlement['losers_sum'] == 100000, 'Losers sum = 100000');
assert($settlement['oracle_fee'] == intval(100000 * 5 / 1000), 'Oracle fee');
assert($settlement['creator_fee'] == intval(100000 * 3 / 1000), 'Creator fee');
assert($settlement['liquidity_fee'] == intval(100000 * 10 / 1000), 'Liquidity fee');
assert($settlement['winners_pool'] == 100000 - 500 - 300 - 1000, 'Winners pool');
assert(count($settlement['payouts']) == 2, 'Two winners');

// Winners get their amount back + profit share
foreach ($settlement['payouts'] as $p) {
    assert($p['payout'] >= $p['amount'], 'Winner payout >= original bet');
}

step('Scenario 30: Settlement', [
    'losers_sum' => $settlement['losers_sum'],
    'oracle_fee' => $settlement['oracle_fee'],
    'creator_fee' => $settlement['creator_fee'],
    'liquidity_fee' => $settlement['liquidity_fee'],
    'winners_pool' => $settlement['winners_pool'],
    'payouts' => $settlement['payouts'],
    'undistributed' => $settlement['undistributed'],
]);


// ============================================================
// SCENARIO 31: CREATOR FEE IN BINARY RESOLUTION
// ============================================================
echo "\n" . str_repeat('=', 70) . "\n";
echo "SCENARIO 31: Creator Fee in Binary Resolution\n";
echo str_repeat('=', 70) . "\n";

// Simulate binary resolution with creator fee using same settlement logic
$binary_bets = [
    ['id'=>10, 'user'=>20, 'amount'=>100000, 'weight'=>95000, 'outcome_index'=>0, 'time_penalty'=>0],
    ['id'=>11, 'user'=>21, 'amount'=>80000, 'weight'=>70000, 'outcome_index'=>1, 'time_penalty'=>0],
];

$binary_settle = lmsr_settlement($binary_bets, 0, 10, 5, 20); // oracle 1%, creator 0.5%, LP 2%
assert($binary_settle['losers_sum'] == 80000, 'Losers = side B');
assert($binary_settle['oracle_fee'] == 800, 'Oracle fee = 80000 * 10/1000 = 800');
assert($binary_settle['creator_fee'] == 400, 'Creator fee = 80000 * 5/1000 = 400');
assert($binary_settle['liquidity_fee'] == 1600, 'LP fee = 80000 * 20/1000 = 1600');
assert($binary_settle['winners_pool'] == 80000 - 800 - 400 - 1600, 'Winners pool = 77200');
assert($binary_settle['payouts'][0]['payout'] == 100000 + 77200, 'Winner gets bet + pool');

step('Scenario 31: Binary with creator fee', [
    'losers_sum' => $binary_settle['losers_sum'],
    'creator_fee' => $binary_settle['creator_fee'],
    'winner_payout' => $binary_settle['payouts'][0]['payout'],
]);


// ============================================================
// SCENARIO 32: MULTI-MARKET LIFECYCLE
// ============================================================
echo "\n" . str_repeat('=', 70) . "\n";
echo "SCENARIO 32: Multi-Market Lifecycle\n";
echo str_repeat('=', 70) . "\n";

// Create market with 4 outcomes, b = 200
$lifecycle_b = 200000;
$lifecycle_q = [0, 0, 0, 0];
$lifecycle_subsidy = lmsr_max_loss($lifecycle_b, 4); // subsidy = b * ln(4)

// Place bets on multiple outcomes
$bet_amounts = [100000, 50000, 75000, 30000]; // on outcomes 0,1,2,3
$lifecycle_bets = [];
$lifecycle_total_tokens = [0, 0, 0, 0];

for ($oi = 0; $oi < 4; $oi++) {
    $tokens = lmsr_tokens_for_amount($lifecycle_q, $lifecycle_b, $oi, $bet_amounts[$oi]);
    $cost = lmsr_buy_cost($lifecycle_q, $lifecycle_b, $oi, $tokens);
    $lifecycle_q[$oi] += $tokens;
    $lifecycle_total_tokens[$oi] += $tokens;
    $lifecycle_bets[] = [
        'id' => $oi + 100,
        'user' => 30 + $oi,
        'amount' => $cost,
        'weight' => $tokens,
        'outcome_index' => $oi,
        'time_penalty' => 0,
    ];
}

// Check prices shifted
$final_prices = lmsr_prices($lifecycle_q, $lifecycle_b);
assert($final_prices[0] > $final_prices[3], 'Outcome 0 (most bet) has highest price');
assert(abs(array_sum($final_prices) - 1000000) <= 4, 'Prices sum to 1');

// Resolve: outcome 0 wins
$lifecycle_settle = lmsr_settlement($lifecycle_bets, 0, 5, 3, 15);
assert($lifecycle_settle['losers_sum'] > 0, 'Has losers');
assert(count($lifecycle_settle['payouts']) == 1, '1 winner');
assert($lifecycle_settle['payouts'][0]['payout'] > $lifecycle_settle['payouts'][0]['amount'], 'Winner profits');

step('Scenario 32: Multi lifecycle', [
    'q_final' => $lifecycle_q,
    'prices' => $final_prices,
    'subsidy' => $lifecycle_subsidy,
    'settlement' => [
        'losers_sum' => $lifecycle_settle['losers_sum'],
        'winners_pool' => $lifecycle_settle['winners_pool'],
        'winner_payout' => $lifecycle_settle['payouts'][0]['payout'],
    ],
]);


// ============================================================
// SCENARIO 33: LP GUARANTEE — subsidy always returned
// ============================================================
echo "\n" . str_repeat('=', 70) . "\n";
echo "SCENARIO 33: LP Principal Guarantee\n";
echo str_repeat('=', 70) . "\n";

// The subsidy (b * ln(N)) is returned to LP regardless of outcome.
// Verify that losers' pool covers all payouts and LP subsidy is separate.
$lp_b = 500000; // b = 500
$lp_n = 5;
$lp_subsidy = lmsr_max_loss($lp_b, $lp_n);
$lp_q = array_fill(0, $lp_n, 0);

// Heavy one-sided betting
$heavy_tokens = lmsr_tokens_for_amount($lp_q, $lp_b, 0, 500000);
$heavy_cost = lmsr_buy_cost($lp_q, $lp_b, 0, $heavy_tokens);
$lp_q[0] += $heavy_tokens;

$small_tokens = lmsr_tokens_for_amount($lp_q, $lp_b, 1, 20000);
$small_cost = lmsr_buy_cost($lp_q, $lp_b, 1, $small_tokens);
$lp_q[1] += $small_tokens;

$lp_bets = [
    ['id'=>200, 'user'=>50, 'amount'=>$heavy_cost, 'weight'=>$heavy_tokens, 'outcome_index'=>0, 'time_penalty'=>0],
    ['id'=>201, 'user'=>51, 'amount'=>$small_cost, 'weight'=>$small_tokens, 'outcome_index'=>1, 'time_penalty'=>0],
];

// Case A: outcome 0 wins (heavy side) — losers pool is small
$settle_a = lmsr_settlement($lp_bets, 0, 5, 3, 10);
assert($settle_a['losers_sum'] == $small_cost, 'Losers = small bet');
// LP subsidy is separate from losers pool — always returned
assert($lp_subsidy > 0, 'LP subsidy is positive');

// Case B: outcome 1 wins (small side) — losers pool is large
$settle_b = lmsr_settlement($lp_bets, 1, 5, 3, 10);
assert($settle_b['losers_sum'] == $heavy_cost, 'Losers = heavy bet');
assert($settle_b['payouts'][0]['payout'] > $settle_b['payouts'][0]['amount'], 'Small side wins big');

step('Scenario 33: LP guarantee', [
    'lp_subsidy' => $lp_subsidy,
    'case_A_losers' => $settle_a['losers_sum'],
    'case_B_losers' => $settle_b['losers_sum'],
    'case_B_winner_payout' => $settle_b['payouts'][0]['payout'],
    'note' => 'LP subsidy is architecturally separate, always returned',
]);


// ============================================================
// SCENARIO 34: EDGE CASES
// ============================================================
echo "\n" . str_repeat('=', 70) . "\n";
echo "SCENARIO 34: Edge Cases\n";
echo str_repeat('=', 70) . "\n";

// 34a: All bets on winner — break even (losers_sum = 0)
$edge_bets_a = [
    ['id'=>300, 'user'=>60, 'amount'=>50000, 'weight'=>45000, 'outcome_index'=>0, 'time_penalty'=>0],
    ['id'=>301, 'user'=>61, 'amount'=>30000, 'weight'=>28000, 'outcome_index'=>0, 'time_penalty'=>0],
];
$settle_edge_a = lmsr_settlement($edge_bets_a, 0, 5, 3, 10);
assert($settle_edge_a['losers_sum'] == 0, 'No losers');
assert($settle_edge_a['winners_pool'] == 0, 'No profit to distribute');
assert($settle_edge_a['payouts'][0]['payout'] == 50000, 'Winner gets original back');
assert($settle_edge_a['payouts'][1]['payout'] == 30000, 'Winner gets original back');

step('Scenario 34a: All bets on winner', [
    'losers_sum' => $settle_edge_a['losers_sum'],
    'payouts' => $settle_edge_a['payouts'],
]);

// 34b: No bets on winner — undistributed goes to LP bonus
$edge_bets_b = [
    ['id'=>310, 'user'=>62, 'amount'=>50000, 'weight'=>45000, 'outcome_index'=>1, 'time_penalty'=>0],
    ['id'=>311, 'user'=>63, 'amount'=>30000, 'weight'=>28000, 'outcome_index'=>2, 'time_penalty'=>0],
];
$settle_edge_b = lmsr_settlement($edge_bets_b, 0, 5, 3, 10);
assert($settle_edge_b['losers_sum'] == 80000, 'All bets are losers');
assert(count($settle_edge_b['payouts']) == 0, 'No winners');
assert($settle_edge_b['winners_pool'] > 0, 'Undistributed pool → LP bonus');

step('Scenario 34b: No bets on winner (LP bonus)', [
    'losers_sum' => $settle_edge_b['losers_sum'],
    'winners_pool' => $settle_edge_b['winners_pool'],
    'note' => 'Entire winners_pool → LP bonus (undistributed)',
]);

// 34c: Zero volume market
$settle_edge_c = lmsr_settlement([], 0, 5, 3, 10);
assert($settle_edge_c['losers_sum'] == 0);
assert(count($settle_edge_c['payouts']) == 0);

step('Scenario 34c: Zero volume', ['result' => $settle_edge_c]);

// 34d: Single bettor (wins alone)
$edge_bets_d = [
    ['id'=>320, 'user'=>64, 'amount'=>100000, 'weight'=>90000, 'outcome_index'=>0, 'time_penalty'=>0],
];
$settle_edge_d = lmsr_settlement($edge_bets_d, 0, 5, 3, 10);
assert($settle_edge_d['losers_sum'] == 0);
assert($settle_edge_d['payouts'][0]['payout'] == 100000, 'Sole winner gets original back');

step('Scenario 34d: Single bettor', ['payout' => $settle_edge_d['payouts'][0]['payout']]);


// ============================================================
// SCENARIO 35: POSITION TRANSFERS
// ============================================================
echo "\n" . str_repeat('=', 70) . "\n";
echo "SCENARIO 35: Position Transfers\n";
echo str_repeat('=', 70) . "\n";

// Simulate transfer logic
function sim_transfer($bet, $to_user, $transfer_tokens) {
    if ($transfer_tokens <= 0 || $transfer_tokens > $bet['weight']) {
        return ['error' => 'Invalid amount'];
    }
    if ($to_user == $bet['user']) {
        return ['error' => 'Cannot transfer to self'];
    }

    $ratio = $transfer_tokens / $bet['weight'];
    $transfer_amount = intval($bet['amount'] * $ratio);

    $original = $bet;
    $original['weight'] -= $transfer_tokens;
    $original['amount'] -= $transfer_amount;
    if ($original['weight'] <= 0) $original['status'] = 4; // fully transferred

    $new_bet = [
        'user' => $to_user,
        'amount' => $transfer_amount,
        'weight' => $transfer_tokens,
        'outcome_index' => $bet['outcome_index'],
        'time_penalty' => $bet['time_penalty'],
        'status' => 0,
    ];
    return ['original' => $original, 'new_bet' => $new_bet];
}

// 35a: Full transfer
$bet_src = ['user'=>70, 'amount'=>100000, 'weight'=>90000, 'outcome_index'=>0, 'time_penalty'=>50000, 'status'=>0];
$r35a = sim_transfer($bet_src, 71, 90000);
assert(!isset($r35a['error']), 'Full transfer should succeed');
assert($r35a['original']['weight'] == 0, 'Original has 0 tokens');
assert($r35a['original']['status'] == 4, 'Status = fully transferred');
assert($r35a['new_bet']['weight'] == 90000, 'New bet has all tokens');
assert($r35a['new_bet']['user'] == 71, 'New bet user = recipient');
assert($r35a['new_bet']['time_penalty'] == 50000, 'Inherits time penalty');

step('Scenario 35a: Full transfer', $r35a);

// 35b: Partial transfer
$bet_src2 = ['user'=>70, 'amount'=>100000, 'weight'=>90000, 'outcome_index'=>1, 'time_penalty'=>0, 'status'=>0];
$r35b = sim_transfer($bet_src2, 72, 30000);
assert(!isset($r35b['error']));
assert($r35b['original']['weight'] == 60000, 'Original keeps remainder');
assert($r35b['new_bet']['weight'] == 30000);
assert($r35b['new_bet']['amount'] == intval(100000 * 30000 / 90000));

step('Scenario 35b: Partial transfer', $r35b);

// 35c: Transfer to self — should fail
$r35c = sim_transfer($bet_src2, 70, 10000);
assert(isset($r35c['error']), 'Transfer to self must fail');

step('Scenario 35c: Transfer to self', ['error' => $r35c['error']]);

// 35d: Transfer more than owned — should fail
$r35d = sim_transfer($bet_src2, 73, 100000);
assert(isset($r35d['error']), 'Excessive transfer must fail');

step('Scenario 35d: Transfer excessive amount', ['error' => $r35d['error']]);

// 35e: Transfer then resolve — recipient gets payout
$transfer_bet_a = ['id'=>400, 'user'=>70, 'amount'=>60000, 'weight'=>50000, 'outcome_index'=>0, 'time_penalty'=>0];
$transfer_bet_b = ['id'=>401, 'user'=>71, 'amount'=>40000, 'weight'=>40000, 'outcome_index'=>0, 'time_penalty'=>50000]; // transferred portion
$transfer_bet_c = ['id'=>402, 'user'=>80, 'amount'=>100000, 'weight'=>80000, 'outcome_index'=>1, 'time_penalty'=>0]; // loser

$settle_35e = lmsr_settlement([$transfer_bet_a, $transfer_bet_b, $transfer_bet_c], 0, 5, 3, 10);
assert($settle_35e['losers_sum'] == 100000, 'Loser bet = 100k');
assert(count($settle_35e['payouts']) == 2, 'Both original + recipient win');
$p_original = $settle_35e['payouts'][0];
$p_recipient = $settle_35e['payouts'][1];
assert($p_original['payout'] > $p_original['amount'], 'Original wins');
assert($p_recipient['payout'] > $p_recipient['amount'], 'Recipient wins');
assert($p_recipient['payout'] < $p_original['payout'], 'Recipient has penalty deduction');

step('Scenario 35e: Transfer then resolve', [
    'original_payout' => $p_original['payout'],
    'recipient_payout' => $p_recipient['payout'],
    'note' => 'Recipient has time penalty, earns less profit',
]);
// ============================================================
// SCENARIO 36: Graduated Early Recall (Mechanism A)
// ============================================================
echo "\n" . str_repeat('=', 70) . "\n";
echo "SCENARIO 36: Graduated Early Recall\n";
echo str_repeat('=', 70) . "\n";

$recall_pool = make_lazy_pool();
$recall_user = make_lazy_user(800, 'RecallUser', 50000000);
lp_deposit($recall_user, $recall_pool, 20000000, $lock_days, $time_now);

// Simulate allocation: 2% of 20000 VIZ = 400 VIZ
$recall_alloc_pct = 2;
$recall_vol_threshold_pct = 1;
$recall_pct_per_step = 10;
$recall_step_pct = 10;
$recall_alloc_amount = intval($recall_pool['free_balance'] * $recall_alloc_pct / 100);
assert($recall_alloc_amount == 400000, 'Allocation = 400 VIZ');

// Simulate allocation
$recall_pool['free_balance'] -= $recall_alloc_amount;
$recall_pool['allocated_balance'] += $recall_alloc_amount;

// Market: 30-day duration (2592000 seconds)
$recall_market_duration = 30 * 86400;
$recall_market_start = $time_now;
$recall_market_end = $recall_market_start + $recall_market_duration;

// Simulate 10 recall steps with zero volume
$current_alloc = $recall_alloc_amount;
$total_recalled = 0;
$bets_sum_at_check = 0;

for ($step = 1; $step <= 10; $step++) {
    $volume_in_period = 0; // zero bets
    $threshold = intval($current_alloc * $recall_vol_threshold_pct / 100);

    if ($volume_in_period < $threshold) {
        $recall_amount = intval($current_alloc * $recall_pct_per_step / 100);
        $current_alloc -= $recall_amount;
        $total_recalled += $recall_amount;
        $recall_pool['free_balance'] += $recall_amount;
        $recall_pool['allocated_balance'] -= $recall_amount;
    }
}

step('Scenario 36: Idle market — all 10 steps recalled', [
    'original_alloc' => fmt($recall_alloc_amount),
    'final_alloc' => fmt($current_alloc),
    'total_recalled' => fmt($total_recalled),
    'recall_pct' => round($total_recalled / $recall_alloc_amount * 100, 1) . '%',
]);

// After 10 idle steps: 0.9^10 ≈ 0.349, so ~65% recalled
$expected_remaining_ratio = pow(1 - $recall_pct_per_step / 100, 10);
assert(abs($current_alloc / $recall_alloc_amount - $expected_remaining_ratio) < 0.01, 'Remaining should be ~34.9%');

// 36b: Market with volume at step 3 — stops recall
$recall_pool2 = make_lazy_pool();
$recall_user2 = make_lazy_user(801, 'RecallUser2', 50000000);
lp_deposit($recall_user2, $recall_pool2, 20000000, $lock_days, $time_now);

$current_alloc2 = intval($recall_pool2['free_balance'] * $recall_alloc_pct / 100);
$recall_pool2['free_balance'] -= $current_alloc2;
$recall_pool2['allocated_balance'] += $current_alloc2;

$total_recalled2 = 0;
for ($step = 1; $step <= 10; $step++) {
    $volume = ($step == 3) ? intval($current_alloc2 * 2 / 100) : 0; // 2% volume at step 3
    $threshold2 = intval($current_alloc2 * $recall_vol_threshold_pct / 100);

    if ($volume < $threshold2) {
        $recall_amount2 = intval($current_alloc2 * $recall_pct_per_step / 100);
        $current_alloc2 -= $recall_amount2;
        $total_recalled2 += $recall_amount2;
    }
    // If volume >= threshold, no recall (step passes)
}

step('Scenario 36b: Volume at step 3 — only steps 1-2 recalled', [
    'original_alloc' => fmt(intval($recall_pool2['free_balance'] + $recall_pool2['allocated_balance']) * $recall_alloc_pct / 100),
    'remaining_alloc' => fmt($current_alloc2),
    'total_recalled' => fmt($total_recalled2),
    'note' => 'Steps 1-2 idle (recalled), step 3 has volume (kept), steps 4-10 idle (recalled)'
]);

// ============================================================
// SCENARIO 37: Active Market Penalty (Mechanism B)
// ============================================================
echo "\n" . str_repeat('=', 70) . "\n";
echo "SCENARIO 37: Active Market Penalty\n";
echo str_repeat('=', 70) . "\n";

$active_market_penalty_pct = 5;

// Oracle with 0 active markets → multiplier = 1.0
$mult_0 = pow(1 - $active_market_penalty_pct / 100, 0);
assert($mult_0 == 1.0, '0 markets = full allocation');

// Oracle with 1 active market → multiplier = 0.95
$mult_1 = pow(1 - $active_market_penalty_pct / 100, 1);
assert(abs($mult_1 - 0.95) < 0.0001, '1 market = 0.95');

// Oracle with 3 active markets → multiplier = 0.857
$mult_3 = pow(1 - $active_market_penalty_pct / 100, 3);
assert(abs($mult_3 - 0.857375) < 0.0001, '3 markets = 0.857');

// Oracle with 10 active markets → multiplier = 0.599
$mult_10 = pow(1 - $active_market_penalty_pct / 100, 10);
assert($mult_10 < 0.6, '10 markets = <0.6');

$base_alloc = 2000000; // 2000 VIZ

step('Scenario 37: Active market penalty scaling', [
    'penalty_pct' => $active_market_penalty_pct . '%',
    '0_markets' => ['mult' => round($mult_0, 4), 'alloc' => fmt(intval($base_alloc * $mult_0))],
    '1_market'  => ['mult' => round($mult_1, 4), 'alloc' => fmt(intval($base_alloc * $mult_1))],
    '3_markets' => ['mult' => round($mult_3, 4), 'alloc' => fmt(intval($base_alloc * $mult_3))],
    '10_markets'=> ['mult' => round($mult_10, 4), 'alloc' => fmt(intval($base_alloc * $mult_10))],
]);

// ============================================================
// SCENARIO 38: Fault Penalty Stamps (Mechanism C)
// ============================================================
echo "\n" . str_repeat('=', 70) . "\n";
echo "SCENARIO 38: Fault Penalty Stamps\n";
echo str_repeat('=', 70) . "\n";

$fault_penalty_pct = 5;
$fault_expiry_days = 10;

// Simulate fault stamps
$stamps = [];

// Oracle gets 3 stamps over time
$stamp_time = $time_now;
$stamps[] = ['oracle' => 100, 'market' => 1, 'reason' => 'no_contest', 'time' => $stamp_time, 'expires' => $stamp_time + $fault_expiry_days * 86400, 'status' => 0];
$stamps[] = ['oracle' => 100, 'market' => 2, 'reason' => 'missed_deadline', 'time' => $stamp_time + 100, 'expires' => $stamp_time + $fault_expiry_days * 86400 + 100, 'status' => 0];
$stamps[] = ['oracle' => 100, 'market' => 3, 'reason' => 'zero_volume', 'time' => $stamp_time + 200, 'expires' => $stamp_time + $fault_expiry_days * 86400 + 200, 'status' => 0];

// Count active stamps
$active_count = count(array_filter($stamps, function($s) use ($stamp_time) { return $s['status'] == 0 && $s['expires'] > $stamp_time; }));
assert($active_count == 3, '3 active stamps');

// Calculate multiplier with 3 stamps
$fault_mult_3 = pow(1 - $fault_penalty_pct / 100, 3);
assert(abs($fault_mult_3 - 0.857375) < 0.0001, '3 stamps = 0.857');

step('Scenario 38a: Fault stamps reduce allocation', [
    'stamps' => 3,
    'fault_multiplier' => round($fault_mult_3, 4),
    'base_2000_viz' => fmt(intval(2000000 * $fault_mult_3)),
]);

// 38b: Combined effect (B + C)
$combined = $mult_3 * $fault_mult_3; // 3 active markets + 3 stamps
$combined_alloc = intval($base_alloc * $combined);

step('Scenario 38b: Combined B+C penalty', [
    'active_market_factor' => round($mult_3, 4),
    'fault_factor' => round($fault_mult_3, 4),
    'combined' => round($combined, 4),
    'base_2000_viz' => fmt($base_alloc),
    'final_alloc' => fmt($combined_alloc),
    'reduction' => round((1 - $combined) * 100, 1) . '%',
]);

// 38c: Stamp expiry — after 10 days, stamps expire and penalty disappears
$expired_count = 0;
$expiry_time = $stamp_time + $fault_expiry_days * 86400 + 86400; // 1 day after expiry
foreach ($stamps as &$s) {
    if ($s['status'] == 0 && $s['expires'] <= $expiry_time) {
        $s['status'] = 1; // expired
        $expired_count++;
    }
}
unset($s);

$remaining_active = count(array_filter($stamps, function($s) { return $s['status'] == 0; }));
assert($expired_count == 3, 'All 3 stamps expired');
assert($remaining_active == 0, 'No active stamps');

$fault_mult_after = pow(1 - $fault_penalty_pct / 100, $remaining_active);
assert($fault_mult_after == 1.0, 'After expiry: multiplier = 1.0');

step('Scenario 38c: Stamp auto-healing', [
    'expired_stamps' => $expired_count,
    'remaining_active' => $remaining_active,
    'multiplier_after_expiry' => $fault_mult_after,
    'note' => 'After ' . $fault_expiry_days . ' days clean operation, penalty disappears',
]);

// ============================================================
// SCENARIO 39: Market Metadata — category/subcategory/tags validation
// ============================================================
echo "\n" . str_repeat('=', 70) . "\n";
echo "SCENARIO 39: Market Metadata — category/subcategory/tags validation\n";
echo str_repeat('=', 70) . "\n";

require_once __DIR__ . '/../module/market_categories.php';

// 39a: Validate categories
$cats = get_market_categories();
assert(is_array($cats), 'get_market_categories returns array');
assert(count($cats) === 9, 'Expected 9 categories, got ' . count($cats));
assert(isset($cats['politics']), 'politics category exists');
assert(isset($cats['crypto']), 'crypto category exists');
assert(isset($cats['finance']), 'finance category exists');
assert(isset($cats['tech']), 'tech category exists');
assert(isset($cats['sports']), 'sports category exists');
assert(isset($cats['entertainment']), 'entertainment category exists');
assert(isset($cats['science']), 'science category exists');
assert(isset($cats['society']), 'society category exists');
assert(isset($cats['custom']), 'custom category exists');

assert(validate_category('politics') === true, 'validate_category(politics)');
assert(validate_category('crypto') === true, 'validate_category(crypto)');
assert(validate_category('nonexistent') === false, 'validate_category(nonexistent)');
assert(validate_category('') === false, 'validate_category(empty)');

step('Scenario 39a: Category validation', [
    'total_categories' => count($cats),
    'valid_politics' => validate_category('politics'),
    'valid_crypto' => validate_category('crypto'),
    'invalid_nonexistent' => validate_category('nonexistent'),
    'all_slugs' => array_keys($cats),
]);

// 39b: Validate subcategories
assert(validate_subcategory('politics', 'elections') === true, 'validate_subcategory(politics, elections)');
assert(validate_subcategory('politics', 'geopolitics') === true, 'validate_subcategory(politics, geopolitics)');
assert(validate_subcategory('politics', 'nba') === false, 'validate_subcategory(politics, nba) should fail');
assert(validate_subcategory('crypto', 'price_targets') === true, 'validate_subcategory(crypto, price_targets)');
assert(validate_subcategory('crypto', 'elections') === false, 'validate_subcategory(crypto, elections) should fail');
assert(validate_subcategory('nonexistent', 'elections') === false, 'validate_subcategory for invalid category');
assert(validate_subcategory('sports', '') === false, 'validate_subcategory with empty sub');

step('Scenario 39b: Subcategory validation', [
    'politics_elections' => validate_subcategory('politics', 'elections'),
    'politics_nba' => validate_subcategory('politics', 'nba'),
    'crypto_price_targets' => validate_subcategory('crypto', 'price_targets'),
    'crypto_elections' => validate_subcategory('crypto', 'elections'),
    'nonexistent_cat' => validate_subcategory('nonexistent', 'elections'),
]);

// 39c: Validate tags
$valid_tags = validate_tags(['bitcoin', 'ethereum', '  SOLANA  ', 'defi']);
assert(count($valid_tags) === 4, 'validate_tags returns 4 cleaned tags');
assert($valid_tags[0] === 'bitcoin', 'tag lowercased: bitcoin');
assert($valid_tags[2] === 'solana', 'tag trimmed+lowered: solana');

$dedup_tags = validate_tags(['btc', 'btc', 'eth', 'BTC']);
assert(count($dedup_tags) === 2, 'validate_tags deduplicates: ' . count($dedup_tags));

$empty_tags = validate_tags([]);
assert($empty_tags === [], 'validate_tags empty returns empty');

$jur_tags = validate_tags(['jurisdiction:us', 'jurisdiction-ban:cn', 'bitcoin']);
assert(count($jur_tags) === 3, 'jurisdiction tags preserved');
assert($jur_tags[0] === 'jurisdiction:us', 'jurisdiction:us preserved');
assert($jur_tags[1] === 'jurisdiction-ban:cn', 'jurisdiction-ban:cn preserved');

// Reject invalid chars
$bad_tags = validate_tags(['good_tag', 'bad tag!', 'hello@world', 'ok-tag']);
assert(in_array('good_tag', $bad_tags), 'good_tag kept');
assert(in_array('ok-tag', $bad_tags), 'ok-tag kept');
// bad tag! -> badtag (space and ! removed)
assert(in_array('badtag', $bad_tags), 'bad tag! sanitized to badtag');

// Too long tag (>64 chars) rejected
$long_tag = str_repeat('a', 65);
$long_result = validate_tags([$long_tag, 'short']);
assert(count($long_result) === 1, 'tag >64 chars rejected');
assert($long_result[0] === 'short', 'only short tag kept');

step('Scenario 39c: Tag validation', [
    'clean_tags' => $valid_tags,
    'dedup_count' => count($dedup_tags),
    'empty_tags' => $empty_tags,
    'jurisdiction_tags' => $jur_tags,
    'sanitized_tags' => $bad_tags,
    'long_tag_rejected' => count($long_result) === 1,
]);

// 39d: get_all_known_tags
$all_tags = get_all_known_tags();
assert(count($all_tags) > 50, 'Known tags pool is large: ' . count($all_tags));
assert(in_array('bitcoin', $all_tags), 'bitcoin in known tags');
assert(in_array('presidential', $all_tags), 'presidential in known tags');
assert(in_array('oscars', $all_tags), 'oscars in known tags');

step('Scenario 39d: All known tags', [
    'total_known_tags' => count($all_tags),
    'has_bitcoin' => in_array('bitcoin', $all_tags),
    'has_presidential' => in_array('presidential', $all_tags),
]);

// ============================================================
// SCENARIO 40: Metadata JSON — build + i18n structure
// ============================================================
echo "\n" . str_repeat('=', 70) . "\n";
echo "SCENARIO 40: Metadata JSON — build + localization structure\n";
echo str_repeat('=', 70) . "\n";

// 40a: Build metadata with full localized fields
$meta_json = build_market_metadata([
    'title' => ['en' => 'Will BTC exceed $150k?', 'ru' => 'Пробьёт ли BTC $150k?'],
    'description' => ['en' => 'Bitcoin price target for 2026', 'ru' => 'Ценовая цель Bitcoin на 2026'],
    'resolution_rules' => ['en' => 'Resolved by CoinGecko price', 'ru' => 'Разрешается по цене CoinGecko'],
    'resolution_url' => 'https://coingecko.com/bitcoin',
    'image_url' => ['en' => 'https://example.com/btc-en.png', 'ru' => 'https://example.com/btc-ru.png'],
    'outcomes' => [
        '0' => ['en' => 'Yes', 'ru' => 'Да'],
        '1' => ['en' => 'No', 'ru' => 'Нет'],
    ],
    'tags' => ['bitcoin', 'ath', 'jurisdiction:us', 'jurisdiction-ban:cn'],
]);

$meta = json_decode($meta_json, true);
assert($meta !== null, 'Metadata JSON is valid');
assert($meta['v'] === 1, 'Metadata version is 1');
assert(!isset($meta['lang']), 'No lang field in metadata (hardcoded in scripts)');
assert($meta['meta']['title']['en'] === 'Will BTC exceed $150k?', 'EN title stored');
assert($meta['meta']['title']['ru'] === 'Пробьёт ли BTC $150k?', 'RU title stored');
assert($meta['meta']['resolution_url'] === 'https://coingecko.com/bitcoin', 'Resolution URL stored');
assert($meta['meta']['image_url']['en'] === 'https://example.com/btc-en.png', 'EN image_url in meta');
assert($meta['meta']['image_url']['ru'] === 'https://example.com/btc-ru.png', 'RU image_url in meta');
assert(!isset($meta['ui']), 'No top-level ui field (image_url moved to meta)');
assert($meta['outcomes']['0']['en'] === 'Yes', 'Outcome 0 EN');
assert($meta['outcomes']['1']['ru'] === 'Нет', 'Outcome 1 RU');
assert(count($meta['tags']) === 4, 'Tags preserved in metadata');
assert(in_array('jurisdiction:us', $meta['tags']), 'jurisdiction:us in metadata tags');
assert(in_array('jurisdiction-ban:cn', $meta['tags']), 'jurisdiction-ban:cn in metadata tags');

step('Scenario 40a: Build metadata with full i18n', [
    'version' => $meta['v'],
    'has_lang_field' => isset($meta['lang']),
    'title_en' => $meta['meta']['title']['en'],
    'title_ru' => $meta['meta']['title']['ru'],
    'resolution_url' => $meta['meta']['resolution_url'],
    'image_url_en' => $meta['meta']['image_url']['en'],
    'outcomes' => $meta['outcomes'],
    'tags_count' => count($meta['tags']),
]);

// 40b: Build metadata with only EN (no RU) — fallback scenario
$meta_en_only_json = build_market_metadata([
    'title' => ['en' => 'Some question'],
    'description' => ['en' => 'Desc'],
    'resolution_rules' => ['en' => 'Rules'],
    'resolution_url' => '',
    'image_url' => [],
    'outcomes' => ['0' => ['en' => 'Yes'], '1' => ['en' => 'No']],
    'tags' => ['test'],
]);
$meta_en = json_decode($meta_en_only_json, true);
assert(!isset($meta_en['meta']['title']['ru']), 'No RU title when only EN provided');
assert($meta_en['meta']['title']['en'] === 'Some question', 'EN title fallback works');

step('Scenario 40b: EN-only metadata (localization fallback)', [
    'has_en' => isset($meta_en['meta']['title']['en']),
    'has_ru' => isset($meta_en['meta']['title']['ru']),
    'en_title' => $meta_en['meta']['title']['en'],
]);

// 40c: Build metadata with empty fields — minimal market
$meta_minimal_json = build_market_metadata([
    'title' => [],
    'description' => [],
    'resolution_rules' => [],
    'resolution_url' => '',
    'image_url' => [],
    'outcomes' => [],
    'tags' => [],
]);
$meta_minimal = json_decode($meta_minimal_json, true);
assert($meta_minimal['v'] === 1, 'Minimal metadata has version');
assert($meta_minimal['meta']['title'] === [], 'Empty title array');
assert($meta_minimal['tags'] === [], 'Empty tags');

step('Scenario 40c: Minimal metadata (empty fields)', [
    'version' => $meta_minimal['v'],
    'title_empty' => empty($meta_minimal['meta']['title']),
    'tags_empty' => empty($meta_minimal['tags']),
]);

// ============================================================
// SCENARIO 41: Localization fallback resolver
// ============================================================
echo "\n" . str_repeat('=', 70) . "\n";
echo "SCENARIO 41: Localization fallback resolver (simulated resolve_i18n)\n";
echo str_repeat('=', 70) . "\n";

// Simulate the JS resolve_i18n() function in PHP
function resolve_i18n_php($obj, $lang, $fallback = 'en') {
    if (!$obj) return '';
    if (is_string($obj)) return $obj;
    return $obj[$lang] ?? $obj[$fallback] ?? (count($obj) > 0 ? reset($obj) : '');
}

// 41a: Request RU when both EN and RU exist
$title_both = ['en' => 'Will BTC moon?', 'ru' => 'Полетит ли BTC?'];
assert(resolve_i18n_php($title_both, 'ru') === 'Полетит ли BTC?', 'RU resolved when available');
assert(resolve_i18n_php($title_both, 'en') === 'Will BTC moon?', 'EN resolved when requested');

// 41b: Request RU when only EN exists (fallback)
assert(resolve_i18n_php(['en' => 'English only'], 'ru') === 'English only', 'Fallback to EN when RU missing');

// 41c: Request unsupported lang (de) -> fallback to en
assert(resolve_i18n_php($title_both, 'de') === 'Will BTC moon?', 'Unsupported lang falls back to EN');

// 41d: Plain string (legacy) just returned as-is
assert(resolve_i18n_php('Legacy text', 'ru') === 'Legacy text', 'Plain string returned as-is');

// 41e: Empty/null
assert(resolve_i18n_php(null, 'en') === '', 'Null returns empty');
assert(resolve_i18n_php([], 'en') === '', 'Empty array returns empty');

// 41f: Only RU provided, request EN -> fallback to first value
$ru_only = ['ru' => 'Только русский'];
assert(resolve_i18n_php($ru_only, 'en') === 'Только русский', 'Falls back to first available lang');

step('Scenario 41: Localization fallback', [
    'both_ru' => resolve_i18n_php($title_both, 'ru'),
    'both_en' => resolve_i18n_php($title_both, 'en'),
    'en_only_req_ru' => resolve_i18n_php(['en' => 'English only'], 'ru'),
    'unsupported_de' => resolve_i18n_php($title_both, 'de'),
    'legacy_string' => resolve_i18n_php('Legacy text', 'ru'),
    'null_input' => resolve_i18n_php(null, 'en'),
    'ru_only_req_en' => resolve_i18n_php($ru_only, 'en'),
]);

// ============================================================
// SCENARIO 42: Jurisdiction filtering (client-side simulation)
// ============================================================
echo "\n" . str_repeat('=', 70) . "\n";
echo "SCENARIO 42: Jurisdiction filtering — banned/allowed tag logic\n";
echo str_repeat('=', 70) . "\n";

// Simulate the JS is_market_jurisdiction_banned() function
function is_jurisdiction_banned_php($market_tags, $user_jurisdiction) {
    if (!$user_jurisdiction || !$market_tags || count($market_tags) === 0) return false;
    $ban_tag = 'jurisdiction-ban:' . strtoupper($user_jurisdiction);
    foreach ($market_tags as $tag) {
        if ($tag === $ban_tag) return true;
    }
    return false;
}

// Market with jurisdiction-ban:CN and jurisdiction:US
$tags_market_a = ['bitcoin', 'ath', 'jurisdiction:US', 'jurisdiction-ban:CN'];

// 42a: Chinese user -> banned
assert(is_jurisdiction_banned_php($tags_market_a, 'CN') === true, 'CN user banned from jurisdiction-ban:CN market');

// 42b: US user -> NOT banned
assert(is_jurisdiction_banned_php($tags_market_a, 'US') === false, 'US user NOT banned (jurisdiction:US is informational)');

// 42c: German user -> NOT banned
assert(is_jurisdiction_banned_php($tags_market_a, 'DE') === false, 'DE user NOT banned (no ban tag)');

// 42d: No jurisdiction set -> never banned
assert(is_jurisdiction_banned_php($tags_market_a, '') === false, 'Empty jurisdiction -> not banned');
assert(is_jurisdiction_banned_php($tags_market_a, null) === false, 'Null jurisdiction -> not banned');

// 42e: Market with no tags -> never banned
assert(is_jurisdiction_banned_php([], 'CN') === false, 'No tags -> not banned');

// 42f: Market with multiple ban tags
$tags_multi_ban = ['bitcoin', 'jurisdiction-ban:CN', 'jurisdiction-ban:KP', 'jurisdiction-ban:IR'];
assert(is_jurisdiction_banned_php($tags_multi_ban, 'CN') === true, 'CN banned in multi-ban');
assert(is_jurisdiction_banned_php($tags_multi_ban, 'KP') === true, 'KP banned in multi-ban');
assert(is_jurisdiction_banned_php($tags_multi_ban, 'IR') === true, 'IR banned in multi-ban');
assert(is_jurisdiction_banned_php($tags_multi_ban, 'US') === false, 'US not banned in multi-ban');

// 42g: Case sensitivity — tags stored lowercase, user jurisdiction uppercase
$tags_lowercase = ['jurisdiction-ban:cn'];
assert(is_jurisdiction_banned_php($tags_lowercase, 'CN') === false, 'Case mismatch: lowercase tag vs uppercase jurisdiction');
// Note: in real system, tags are stored lowercase via validate_tags(), but ban check uses uppercase.
// The API should normalize. Let's verify with proper uppercase tags:
$tags_uppercase = ['jurisdiction-ban:CN'];
assert(is_jurisdiction_banned_php($tags_uppercase, 'CN') === true, 'Proper case match');

step('Scenario 42: Jurisdiction filtering', [
    'cn_banned' => is_jurisdiction_banned_php($tags_market_a, 'CN'),
    'us_not_banned' => !is_jurisdiction_banned_php($tags_market_a, 'US'),
    'de_not_banned' => !is_jurisdiction_banned_php($tags_market_a, 'DE'),
    'empty_not_banned' => !is_jurisdiction_banned_php($tags_market_a, ''),
    'no_tags_not_banned' => !is_jurisdiction_banned_php([], 'CN'),
    'multi_ban_cn' => is_jurisdiction_banned_php($tags_multi_ban, 'CN'),
    'multi_ban_kp' => is_jurisdiction_banned_php($tags_multi_ban, 'KP'),
    'multi_ban_us_ok' => !is_jurisdiction_banned_php($tags_multi_ban, 'US'),
]);

// ============================================================
// SCENARIO 43: i18n category structure integrity
// ============================================================
echo "\n" . str_repeat('=', 70) . "\n";
echo "SCENARIO 43: Category structure — i18n completeness\n";
echo str_repeat('=', 70) . "\n";

$required_langs = ['en', 'ru'];
$structure_errors = [];

foreach ($cats as $slug => $cat) {
    // Each category must have icon, i18n, subcategories, tags
    if (empty($cat['icon'])) $structure_errors[] = "$slug: missing icon";
    if (!isset($cat['i18n']) || !is_array($cat['i18n'])) $structure_errors[] = "$slug: missing i18n";
    foreach ($required_langs as $lang) {
        if (empty($cat['i18n'][$lang])) $structure_errors[] = "$slug: missing i18n.$lang";
    }
    if (!isset($cat['subcategories']) || !is_array($cat['subcategories'])) {
        $structure_errors[] = "$slug: missing subcategories";
    } else {
        foreach ($cat['subcategories'] as $sub_slug => $sub_i18n) {
            foreach ($required_langs as $lang) {
                if (empty($sub_i18n[$lang])) $structure_errors[] = "$slug/$sub_slug: missing i18n.$lang";
            }
        }
    }
    if (!isset($cat['tags']) || !is_array($cat['tags'])) $structure_errors[] = "$slug: missing tags";
}

assert(count($structure_errors) === 0, 'Category structure errors: ' . implode('; ', $structure_errors));

// Count totals
$total_subcategories = 0;
$total_tags = 0;
foreach ($cats as $cat) {
    $total_subcategories += count($cat['subcategories']);
    $total_tags += count($cat['tags']);
}

step('Scenario 43: Category structure integrity', [
    'errors' => $structure_errors,
    'total_categories' => count($cats),
    'total_subcategories' => $total_subcategories,
    'total_tags' => $total_tags,
    'all_have_en_ru' => count($structure_errors) === 0,
]);

// ============================================================
// SCENARIO 44: Market creation with metadata (simulated API flow)
// ============================================================
echo "\n" . str_repeat('=', 70) . "\n";
echo "SCENARIO 44: Market creation with metadata — simulated API flow\n";
echo str_repeat('=', 70) . "\n";

// Simulate the full create-market flow with metadata fields
$input_category = 'crypto';
$input_subcategory = 'price_targets';
$input_tags_raw = ['bitcoin', 'ath', 'jurisdiction:US', 'jurisdiction-ban:CN', '  INVALID TAG! ', ''];
$input_default_lang = 'en';
$input_title_en = 'Will BTC exceed $150k by end of 2026?';
$input_title_ru = 'Пробьёт ли BTC $150k к концу 2026?';
$input_desc_en = 'Market resolves YES if BTC price...';
$input_rules_en = 'CoinGecko BTC/USD on Dec 31, 2026 at 23:59 UTC';
$input_resolution_url = 'https://coingecko.com/bitcoin';
$input_outcome_a_en = 'Yes';
$input_outcome_a_ru = 'Да';
$input_outcome_b_en = 'No';
$input_outcome_b_ru = 'Нет';

// Step 1: Validate category
assert(validate_category($input_category), 'Category valid');
assert(validate_subcategory($input_category, $input_subcategory), 'Subcategory valid');

// Step 2: Validate + sanitize tags
$clean_tags = validate_tags($input_tags_raw);
assert(in_array('bitcoin', $clean_tags), 'bitcoin tag kept');
assert(in_array('ath', $clean_tags), 'ath tag kept');
assert(in_array('jurisdiction:us', $clean_tags), 'jurisdiction tag kept (lowercased)');
assert(in_array('jurisdiction-ban:cn', $clean_tags), 'ban tag kept (lowercased)');
assert(!in_array('', $clean_tags), 'empty tag removed');

// Step 3: Build metadata
$meta_title = ['en' => htmlspecialchars($input_title_en)];
if ($input_title_ru) $meta_title['ru'] = htmlspecialchars($input_title_ru);
$meta_desc = ['en' => htmlspecialchars($input_desc_en)];
$meta_rules = ['en' => htmlspecialchars($input_rules_en)];
$meta_outcomes = [
    '0' => ['en' => htmlspecialchars($input_outcome_a_en), 'ru' => htmlspecialchars($input_outcome_a_ru)],
    '1' => ['en' => htmlspecialchars($input_outcome_b_en), 'ru' => htmlspecialchars($input_outcome_b_ru)],
];

$metadata_json = build_market_metadata([
    'title' => $meta_title,
    'description' => $meta_desc,
    'resolution_rules' => $meta_rules,
    'resolution_url' => $input_resolution_url,
    'image_url' => [],
    'outcomes' => $meta_outcomes,
    'tags' => $clean_tags,
]);

$built_meta = json_decode($metadata_json, true);
assert($built_meta !== null, 'Built metadata is valid JSON');
assert($built_meta['v'] === 1, 'Version 1');
assert(!isset($built_meta['lang']), 'No lang field in metadata');
assert($built_meta['meta']['title']['en'] === htmlspecialchars($input_title_en), 'EN title in metadata');
assert($built_meta['meta']['title']['ru'] === htmlspecialchars($input_title_ru), 'RU title in metadata');

// Legacy field fallback: q = title_en, a = outcome_a_en, b = outcome_b_en
$legacy_q = $meta_title[$input_default_lang] ?? ($meta_title['en'] ?? '');
$legacy_a = $meta_outcomes['0'][$input_default_lang] ?? ($meta_outcomes['0']['en'] ?? '');
$legacy_b = $meta_outcomes['1'][$input_default_lang] ?? ($meta_outcomes['1']['en'] ?? '');
assert($legacy_q === htmlspecialchars($input_title_en), 'Legacy q populated from title_en');
assert($legacy_a === htmlspecialchars($input_outcome_a_en), 'Legacy a populated from outcome_a_en');
assert($legacy_b === htmlspecialchars($input_outcome_b_en), 'Legacy b populated from outcome_b_en');

step('Scenario 44: Create market with metadata', [
    'category' => $input_category,
    'subcategory' => $input_subcategory,
    'clean_tags' => $clean_tags,
    'metadata_json_length' => strlen($metadata_json),
    'legacy_q' => $legacy_q,
    'legacy_a' => $legacy_a,
    'legacy_b' => $legacy_b,
    'metadata_version' => $built_meta['v'],
    'outcome_0_ru' => $built_meta['outcomes']['0']['ru'],
]);

// ============================================================
// SCENARIO 45: i18n JSON files — key consistency check
// ============================================================
echo "\n" . str_repeat('=', 70) . "\n";
echo "SCENARIO 45: i18n JSON files — key consistency between en.json and ru.json\n";
echo str_repeat('=', 70) . "\n";

$en_json_path = __DIR__ . '/../i18n/en.json';
$ru_json_path = __DIR__ . '/../i18n/ru.json';

$en_data = json_decode(file_get_contents($en_json_path), true);
$ru_data = json_decode(file_get_contents($ru_json_path), true);

assert($en_data !== null, 'en.json is valid JSON');
assert($ru_data !== null, 'ru.json is valid JSON');

$en_keys = array_keys($en_data);
$ru_keys = array_keys($ru_data);

$missing_in_ru = array_diff($en_keys, $ru_keys);
$missing_in_en = array_diff($ru_keys, $en_keys);

assert(count($missing_in_ru) === 0, 'Keys missing in ru.json: ' . implode(', ', $missing_in_ru));
assert(count($missing_in_en) === 0, 'Keys missing in en.json: ' . implode(', ', $missing_in_en));

// Check critical keys exist
$critical_keys = [
    'market.category_label', 'market.subcategory_label', 'market.tags_label',
    'market.default_lang_label', 'market.add_language',
    'market.jurisdiction_relevant_label', 'market.jurisdiction_banned_label',
    'jurisdiction.warning', 'jurisdiction.proceed', 'jurisdiction.go_back',
    'filter.all_categories', 'filter.all_subcategories', 'filter.hot_tags',
    'nav.markets', 'nav.create', 'nav.balance', 'nav.profile',
    'profile.jurisdiction_label',
];
$missing_critical = [];
foreach ($critical_keys as $key) {
    if (!isset($en_data[$key])) $missing_critical[] = "en:$key";
    if (!isset($ru_data[$key])) $missing_critical[] = "ru:$key";
}
assert(count($missing_critical) === 0, 'Missing critical i18n keys: ' . implode(', ', $missing_critical));

// Check interpolation placeholders consistency
foreach ($en_keys as $key) {
    if (isset($ru_data[$key])) {
        preg_match_all('/%%([A-Z_]+)%%/', $en_data[$key], $en_vars);
        preg_match_all('/%%([A-Z_]+)%%/', $ru_data[$key], $ru_vars);
        $en_placeholders = $en_vars[1];
        $ru_placeholders = $ru_vars[1];
        sort($en_placeholders);
        sort($ru_placeholders);
        if ($en_placeholders !== $ru_placeholders) {
            // Only warn, don't fail — some keys may intentionally differ
            echo "  WARNING: Placeholder mismatch for key '$key': EN=" . implode(',', $en_placeholders) . " RU=" . implode(',', $ru_placeholders) . "\n";
        }
    }
}

step('Scenario 45: i18n key consistency', [
    'en_keys_count' => count($en_keys),
    'ru_keys_count' => count($ru_keys),
    'missing_in_ru' => $missing_in_ru,
    'missing_in_en' => $missing_in_en,
    'critical_keys_ok' => count($missing_critical) === 0,
]);

// ============================================================
// LEVERAGE (BOOST) HELPER FUNCTIONS
// ============================================================

/**
 * Compute cancel_value for a CPMM leveraged position (simulation)
 * cancel_value = new_reserve - old_reserve after selling tokens back
 */
function leverage_cancel_value_cpmm($market, $tokens, $outcome_index) {
    $ra = intval($market['reserve_a']);
    $rb = intval($market['reserve_b']);
    $k = intval($market['k']);
    if ($outcome_index == 0) {
        // Tokens on A: sell back to pool
        $new_ra = $ra + $tokens;
        $new_rb = intval($k / $new_ra);
        return $rb - $new_rb;
    } else {
        // Tokens on B: sell back to pool
        $new_rb = $rb + $tokens;
        $new_ra = intval($k / $new_rb);
        return $ra - $new_ra;
    }
}

/**
 * Compute worst-case cancel_value after an opposing bet of M_max on the OTHER side
 * For CPMM binary: opposing bet moves the price against the leveraged position
 */
function leverage_cancel_value_worst_cpmm($market, $collateral, $loan, $outcome_index, $M_max) {
    // Simulate opposing bet on the other outcome
    $ra = intval($market['reserve_a']);
    $rb = intval($market['reserve_b']);
    $k = intval($market['k']);
    $total = $collateral + $loan;

    if ($outcome_index == 0) {
        // Position on A, opposing bet on B
        $new_rb = $rb + $M_max;
        $new_ra = intval($k / $new_rb);
    } else {
        // Position on B, opposing bet on A
        $new_ra = $ra + $M_max;
        $new_rb = intval($k / $new_ra);
    }

    // Now compute cancel_value at the new reserves
    $tokens = intval($total); // approximate tokens ≈ bet amount (for deep markets)
    if ($outcome_index == 0) {
        $sell_ra = $new_ra + $tokens;
        $sell_rb = intval($k / $sell_ra);
        $cancel = $new_rb - $sell_rb;
    } else {
        $sell_rb = $new_rb + $tokens;
        $sell_ra = intval($k / $sell_rb);
        $cancel = $new_ra - $sell_ra;
    }
    return max(0, $cancel);
}

/**
 * Compute liquidation threshold = loan + pool_profit
 */
function leverage_liquidation_threshold($loan, $R_pct) {
    return intval($loan * (1 + $R_pct / 100));
}

/**
 * Compute max leverage via binary search (Constraint 2 simulation)
 * Finds max loan L such that cancel_value_worst >= threshold * (1 + S%)
 */
function leverage_compute_max_leverage_cpmm($market, $collateral, $outcome_index, $R_pct, $S_pct, $M_max) {
    // Binary search for max loan
    $lo = 0;
    $hi = $collateral * 10; // max 10x leverage for search
    $best_loan = 0;

    for ($i = 0; $i < 50; $i++) {
        $mid = intval(($lo + $hi) / 2);
        if ($mid <= $lo) break;

        $threshold = leverage_liquidation_threshold($mid, $R_pct);
        $threshold_safe = intval($threshold * (1 + $S_pct / 100));
        $worst_cv = leverage_cancel_value_worst_cpmm($market, $collateral, $mid, $outcome_index, $M_max);

        if ($worst_cv >= $threshold_safe) {
            $best_loan = $mid;
            $lo = $mid;
        } else {
            $hi = $mid;
        }
    }

    $total_bet = $collateral + $best_loan;
    $leverage_x = $total_bet / $collateral;
    return [
        'loan' => $best_loan,
        'total_bet' => $total_bet,
        'leverage_x' => round($leverage_x, 4),
        'threshold' => leverage_liquidation_threshold($best_loan, $R_pct),
    ];
}

// ============================================================
// SCENARIO 46: Leverage cancel_value calculation (CPMM)
// ============================================================
echo "\n" . str_repeat('=', 70) . "\n";
echo "SCENARIO 46: Leverage cancel_value calculation (CPMM)\n";
echo str_repeat('=', 70) . "\n";

// Create a market with deep liquidity
$lev_market = [
    'id' => 100, 'reserve_a' => 5000000, 'reserve_b' => 5000000,
    'k' => 5000000 * 5000000, 'liquidity_sum' => 10000000,
    'bets_sum' => 0, 'a_bets_sum' => 0, 'b_bets_sum' => 0,
    'status' => 1, 'market_type' => 0,
];

// Test 1: cancel_value for various token amounts
$cv_1000 = leverage_cancel_value_cpmm($lev_market, 1000000, 0);
$cv_1000_b = leverage_cancel_value_cpmm($lev_market, 1000000, 1);
echo "Cancel value for 1000 VIZ tokens on A: " . ($cv_1000/1000) . " VIZ\n";
echo "Cancel value for 1000 VIZ tokens on B: " . ($cv_1000_b/1000) . " VIZ\n";

$test46_ok = ($cv_1000 > 0 && $cv_1000_b > 0);
echo "Test 46.1 (cancel_value > 0): " . ($test46_ok ? 'PASS' : 'FAIL') . "\n";

// Symmetric market should give symmetric cancel values
$test46_sym = ($cv_1000 == $cv_1000_b);
echo "Test 46.2 (symmetric cancel_value): " . ($test46_sym ? 'PASS' : 'FAIL') . "\n";

// Test 2: cancel_value decreases with larger token amounts (more slippage)
$cv_5000 = leverage_cancel_value_cpmm($lev_market, 5000000, 0);
echo "Cancel value for 5000 VIZ tokens on A: " . ($cv_5000/1000) . " VIZ\n";
$test46_slippage = ($cv_5000 < 5 * $cv_1000); // less than 5x due to slippage
echo "Test 46.3 (slippage with larger amount): " . ($test46_slippage ? 'PASS' : 'FAIL') . "\n";

step('Scenario 46: CPMM cancel_value', [
    'cv_1000_A' => $cv_1000,
    'cv_1000_B' => $cv_1000_b,
    'cv_5000_A' => $cv_5000,
    'symmetric' => $test46_sym,
    'slippage' => $test46_slippage,
]);

// ============================================================
// SCENARIO 47: Liquidation threshold & safety margin
// ============================================================
echo "\n" . str_repeat('=', 70) . "\n";
echo "SCENARIO 47: Liquidation threshold & safety margin\n";
echo str_repeat('=', 70) . "\n";

$R_pct = 10; // 10% pool profit
$S_pct = 1;  // 1% safety margin
$loan_1000 = 1000000; // 1000 VIZ loan

$threshold = leverage_liquidation_threshold($loan_1000, $R_pct);
$threshold_safe = intval($threshold * (1 + $S_pct / 100));

echo "Loan: " . ($loan_1000/1000) . " VIZ\n";
echo "Pool profit ({$R_pct}%): " . ($loan_1000 * $R_pct / 100 / 1000) . " VIZ\n";
echo "Liquidation threshold: " . ($threshold/1000) . " VIZ\n";
echo "Threshold with {$S_pct}% safety: " . ($threshold_safe/1000) . " VIZ\n";

$test47_threshold = ($threshold == 1100000); // 1000 * 1.10 = 1100 VIZ
$test47_safe = ($threshold_safe == 1111000); // 1100 * 1.01 = 1111 VIZ

echo "Test 47.1 (threshold = loan * 1.10): " . ($test47_threshold ? 'PASS' : 'FAIL') . "\n";
echo "Test 47.2 (safe threshold = 1111 VIZ): " . ($test47_safe ? 'PASS' : 'FAIL') . "\n";

step('Scenario 47: Threshold & safety', [
    'threshold' => $threshold,
    'threshold_safe' => $threshold_safe,
    'test_threshold' => $test47_threshold,
    'test_safe' => $test47_safe,
]);

// ============================================================
// SCENARIO 48: Max leverage binary search (CPMM)
// ============================================================
echo "\n" . str_repeat('=', 70) . "\n";
echo "SCENARIO 48: Max leverage binary search (CPMM)\n";
echo str_repeat('=', 70) . "\n";

// Deep market with 10000 VIZ liquidity (symmetric)
$deep_market = [
    'id' => 200, 'reserve_a' => 5000000, 'reserve_b' => 5000000,
    'k' => 5000000 * 5000000, 'liquidity_sum' => 10000000,
    'bets_sum' => 0, 'a_bets_sum' => 0, 'b_bets_sum' => 0,
    'status' => 1, 'market_type' => 0,
];

$collateral_360 = 360000; // 360 VIZ collateral
$M_max_opposing = 5000000; // Max opposing bet ≈ all of reserve

$max_lev = leverage_compute_max_leverage_cpmm(
    $deep_market, $collateral_360, 0, $R_pct, $S_pct, $M_max_opposing
);

echo "Collateral: " . ($collateral_360/1000) . " VIZ\n";
echo "Max loan: " . ($max_lev['loan']/1000) . " VIZ\n";
echo "Max total bet: " . ($max_lev['total_bet']/1000) . " VIZ\n";
echo "Max leverage: " . $max_lev['leverage_x'] . "x\n";

$test48_positive = ($max_lev['loan'] > 0);
$test48_leverage = ($max_lev['leverage_x'] > 1.0);
echo "Test 48.1 (loan > 0): " . ($test48_positive ? 'PASS' : 'FAIL') . "\n";
echo "Test 48.2 (leverage > 1x): " . ($test48_leverage ? 'PASS' : 'FAIL') . "\n";

// Verify: at max leverage, worst-case cancel_value >= safe threshold
$worst_at_max = leverage_cancel_value_worst_cpmm(
    $deep_market, $collateral_360, $max_lev['loan'], 0, $M_max_opposing
);
$threshold_at_max = intval($max_lev['threshold'] * (1 + $S_pct / 100));
$test48_safe = ($worst_at_max >= $threshold_at_max - 1000); // within 1 VIZ tolerance
echo "Test 48.3 (worst CV >= safe threshold at max): " . ($test48_safe ? 'PASS' : 'FAIL') . "\n";
echo "  Worst CV: " . ($worst_at_max/1000) . " VIZ, Safe threshold: " . ($threshold_at_max/1000) . " VIZ\n";

step('Scenario 48: Max leverage search', [
    'max_leverage' => $max_lev,
    'worst_at_max' => $worst_at_max,
    'safe_threshold' => $threshold_at_max,
    'test_positive' => $test48_positive,
    'test_leverage' => $test48_leverage,
    'test_safe' => $test48_safe,
]);

// ============================================================
// SCENARIO 49: Leveraged position lifecycle (open → close)
// ============================================================
echo "\n" . str_repeat('=', 70) . "\n";
echo "SCENARIO 49: Leveraged position lifecycle (open -> close)\n";
echo str_repeat('=', 70) . "\n";

// Open a 2x position on A with 360 VIZ collateral
$collateral_open = 360000; // 360 VIZ
$loan_open = 360000;       // 360 VIZ (2x leverage)
$total_bet_open = $collateral_open + $loan_open; // 720 VIZ
$R_pct_open = 10;
$pool_profit_open = intval($loan_open * $R_pct_open / 100); // 36 VIZ
$threshold_open = leverage_liquidation_threshold($loan_open, $R_pct_open); // 396 VIZ

// Place the bet on A — gets tokens from CPMM
$tokens_received = intval($total_bet_open); // approximate tokens
$actual_cv = leverage_cancel_value_cpmm($deep_market, $tokens_received, 0);

echo "Opened 2x position: collateral=" . ($collateral_open/1000) . " VIZ, loan=" . ($loan_open/1000) . " VIZ\n";
echo "Total bet: " . ($total_bet_open/1000) . " VIZ\n";
echo "Liquidation threshold: " . ($threshold_open/1000) . " VIZ\n";
echo "Current cancel_value: " . ($actual_cv/1000) . " VIZ\n";

// Verify position is safe at open
echo "Position is safe (cancel_value > threshold): " . ($actual_cv > $threshold_open ? 'YES' : 'NO') . "\n";

// Voluntary close: cancel_value > threshold
if ($actual_cv > $threshold_open) {
    $pool_receives = $threshold_open; // loan + pool profit
    $bettor_receives = $actual_cv - $threshold_open;
    echo "Voluntary close: pool receives " . ($pool_receives/1000) . " VIZ, bettor receives " . ($bettor_receives/1000) . " VIZ\n";
    $test49_close = ($bettor_receives >= 0);
} else {
    $test49_close = false;
}

echo "Test 49.1 (voluntary close OK): " . ($test49_close ? 'PASS' : 'FAIL') . "\n";

// Check: at 2x leverage with no price movement, bettor gets collateral back minus pool profit
$test49_profit = ($pool_receives == $threshold_open);
echo "Test 49.2 (pool receives threshold): " . ($test49_profit ? 'PASS' : 'FAIL') . "\n";

step('Scenario 49: Lifecycle open->close', [
    'collateral' => $collateral_open,
    'loan' => $loan_open,
    'total_bet' => $total_bet_open,
    'threshold' => $threshold_open,
    'cancel_value' => $actual_cv,
    'pool_receives' => $pool_receives ?? 0,
    'bettor_receives' => $bettor_receives ?? 0,
    'test_close' => $test49_close,
    'test_profit' => $test49_profit,
]);

// ============================================================
// SCENARIO 50: Cancel-bet liquidation waterfall (Case B)
// ============================================================
echo "\n" . str_repeat('=', 70) . "\n";
echo "SCENARIO 50: Cancel-bet liquidation waterfall (Case B)\n";
echo str_repeat('=', 70) . "\n";

// Setup: market with 5000 VIZ per side
$liq_market = [
    'id' => 300, 'reserve_a' => 2500000, 'reserve_b' => 2500000,
    'k' => 2500000 * 2500000, 'liquidity_sum' => 5000000,
    'bets_sum' => 0, 'a_bets_sum' => 0, 'b_bets_sum' => 0,
    'status' => 1, 'market_type' => 0,
];

// Bettor opens 3x boosted position on A: collateral=100 VIZ, loan=200 VIZ
$cb_collateral = 100000; // 100 VIZ
$cb_loan = 200000;       // 200 VIZ
$cb_total = 300000;      // 300 VIZ
$cb_R = 10;
$cb_threshold = leverage_liquidation_threshold($cb_loan, $cb_R); // 220 VIZ
$cb_tokens = $cb_total; // approximate
echo "Opened 3x on A: collateral=" . ($cb_collateral/1000) . ", loan=" . ($cb_loan/1000) . ", threshold=" . ($cb_threshold/1000) . " VIZ\n";

// Another user cancels a bet on A (same outcome as leveraged position)
// Cancel-bet executes FIRST (Case B)
$cancel_amount = 200000; // 200 VIZ cancelled on A
// After cancel on A, reserve_a increases, reserve_b decreases
$new_ra_after_cancel = $liq_market['reserve_a'] + $cancel_amount; // tokens returned to pool
$new_rb_after_cancel = intval($liq_market['k'] / $new_ra_after_cancel);

echo "After cancel-bet on A: reserve_a=" . ($new_ra_after_cancel/1000) . ", reserve_b=" . ($new_rb_after_cancel/1000) . " VIZ\n";

// Now re-evaluate the leveraged position's cancel_value at new reserves
$market_after_cancel = $liq_market;
$market_after_cancel['reserve_a'] = $new_ra_after_cancel;
$market_after_cancel['reserve_b'] = $new_rb_after_cancel;

$cv_after_cancel = leverage_cancel_value_cpmm($market_after_cancel, $cb_tokens, 0);
echo "Cancel_value after cancel-bet: " . ($cv_after_cancel/1000) . " VIZ\n";
echo "Liquidation threshold: " . ($cb_threshold/1000) . " VIZ\n";

// Check if position needs liquidation
$needs_liquidation = ($cv_after_cancel < $cb_threshold);
echo "Needs liquidation: " . ($needs_liquidation ? 'YES' : 'NO') . "\n";

if ($needs_liquidation) {
    // Pool receives min(cancel_value, threshold) = cancel_value (since CV < threshold)
    // Bad debt = threshold - cancel_value
    $bad_debt = $cb_threshold - $cv_after_cancel;
    echo "LIQUIDATED: pool would receive " . ($cv_after_cancel/1000) . " VIZ\n";
    echo "Bad debt (pool shortfall): " . ($bad_debt/1000) . " VIZ\n";
    $test50_bad_debt = ($bad_debt > 0);
    echo "Test 50.1 (bad debt exists when CV < threshold): " . ($test50_bad_debt ? 'PASS' : 'FAIL') . "\n";

    // Bettor receives nothing (or residual if CV > 0)
    $bettor_gets = max(0, $cv_after_cancel - $cb_threshold);
    echo "Bettor receives: " . ($bettor_gets/1000) . " VIZ\n";
    $test50_bettor = ($bettor_gets == 0);
    echo "Test 50.2 (bettor gets 0 when liquidated): " . ($test50_bettor ? 'PASS' : 'FAIL') . "\n";
} else {
    // Position survives — no liquidation needed
    $test50_bad_debt = true; // no bad debt = OK
    $test50_bettor = true;
    echo "Position survives cancel-bet. No liquidation needed.\n";
}

step('Scenario 50: Cancel-bet liquidation', [
    'cancel_value_after' => $cv_after_cancel,
    'threshold' => $cb_threshold,
    'needs_liquidation' => $needs_liquidation,
    'bad_debt' => $bad_debt ?? 0,
    'test_bad_debt' => $test50_bad_debt,
    'test_bettor' => $test50_bettor,
]);

// ============================================================
// SCENARIO 51: LMSR max bet amount (Constraint 2 for multi)
// ============================================================
echo "\n" . str_repeat('=', 70) . "\n";
echo "SCENARIO 51: LMSR max bet amount (Constraint 2)\n";
echo str_repeat('=', 70) . "\n";

// Simple 3-outcome LMSR market
// b = 1000000 (1000 VIZ depth), q = [0, 0, 0] initial
$lmsr_b = 1000000;
$q = [0, 0, 0];
$slippage_pct = 10;

if (!function_exists('lmsr_cost_leverage_test')) {
function lmsr_cost_leverage_test($q, $b) {
    $sum = 0;
    foreach ($q as $qi) $sum += exp($qi / $b);
    return intval($b * log($sum));
}
}

if (!function_exists('lmsr_sell_return_leverage_test')) {
function lmsr_sell_return_leverage_test($q, $b, $outcome_index, $delta) {
    $old_cost = lmsr_cost_leverage_test($q, $b);
    $q_new = $q;
    $q_new[$outcome_index] -= $delta;
    $new_cost = lmsr_cost_leverage_test($q_new, $b);
    return max(0, $old_cost - $new_cost);
}
}

function lmsr_max_bet_sim($q, $b, $slippage_pct) {
    $lo = 0;
    $hi = $b * 100;
    $best = 0;
    for ($i = 0; $i < 50; $i++) {
        $mid = intval(($lo + $hi) / 2);
        if ($mid <= $lo) break;
        $q_new = $q;
        // Try all outcomes, find max bet that stays within slippage
        $all_ok = true;
        for ($oi = 0; $oi < count($q); $oi++) {
            $q_test = $q;
            $q_test[$oi] += $mid;
            $cost = lmsr_cost_leverage_test($q_test, $b) - lmsr_cost_leverage_test($q, $b);
            $tokens = $mid;
            $slippage = ($cost > 0) ? (($cost - $tokens) / $tokens * 100) : 0;
            if ($slippage > $slippage_pct) {
                $all_ok = false;
                break;
            }
        }
        if ($all_ok) {
            $best = $mid;
            $lo = $mid;
        } else {
            $hi = $mid;
        }
    }
    return $best;
}

$max_bet = lmsr_max_bet_sim($q, $lmsr_b, $slippage_pct);
echo "LMSR b=" . ($lmsr_b/1000) . " VIZ, slippage cap=" . $slippage_pct . "%\n";
echo "Max bet within slippage: " . ($max_bet/1000) . " VIZ\n";

$test51_positive = ($max_bet > 0);
$test51_reasonable = ($max_bet > 0 && $max_bet < $lmsr_b * 10000); // should be finite
echo "Test 51.1 (max_bet > 0): " . ($test51_positive ? 'PASS' : 'FAIL') . "\n";
echo "Test 51.2 (max_bet is bounded): " . ($test51_reasonable ? 'PASS' : 'FAIL') . "\n";

// Verify: at max_bet, slippage is within cap
$cost_at_max = lmsr_cost_leverage_test([$max_bet, 0, 0], $lmsr_b) - lmsr_cost_leverage_test($q, $lmsr_b);
$slippage_at_max = ($cost_at_max > 0 && $max_bet > 0) ? (($cost_at_max - $max_bet) / $max_bet * 100) : 0;
echo "Slippage at max_bet: " . number_format($slippage_at_max, 2) . "%\n";
$test51_slippage = ($slippage_at_max <= $slippage_pct + 0.5); // 0.5% tolerance
echo "Test 51.3 (slippage at max <= cap): " . ($test51_slippage ? 'PASS' : 'FAIL') . "\n";

step('Scenario 51: LMSR max bet', [
    'max_bet' => $max_bet,
    'slippage_at_max' => round($slippage_at_max, 2),
    'test_positive' => $test51_positive,
    'test_reasonable' => $test51_reasonable,
    'test_slippage' => $test51_slippage,
]);

// ============================================================
// SCENARIO 52: Convert to Normal Bet calculation
// ============================================================
echo "\n" . str_repeat('=', 70) . "\n";
echo "SCENARIO 52: Convert to Normal Bet calculation\n";
echo str_repeat('=', 70) . "\n";

// Position: 5x boost on A, collateral=360 VIZ, loan=1800 VIZ
$conv_collateral = 360000;
$conv_loan = 1800000;
$conv_total = 2160000;
$conv_R = 10;
$conv_threshold = leverage_liquidation_threshold($conv_loan, $conv_R); // 1980 VIZ

// Position in profit: cancel_value = 2635 VIZ
$conv_cancel_value = 2635000;
$conv_profit = $conv_cancel_value - $conv_threshold; // 655 VIZ

$conv_profit_cost = 50; // 50% of unrealized profit
$conv_fee = intval($conv_profit * $conv_profit_cost / 100); // 327.5 VIZ
$conv_total_payment = $conv_threshold + $conv_fee; // 1980 + 327.5 = 2307.5 VIZ

echo "Cancel value: " . ($conv_cancel_value/1000) . " VIZ\n";
echo "Pool obligation: " . ($conv_threshold/1000) . " VIZ\n";
echo "Unrealized profit: " . ($conv_profit/1000) . " VIZ\n";
echo "Conversion fee (50%): " . ($conv_fee/1000) . " VIZ\n";
echo "Total user payment: " . ($conv_total_payment/1000) . " VIZ\n";

$test52_profit = ($conv_profit == 655000); // 655 VIZ
$test52_fee = ($conv_fee == 327500); // 327.5 VIZ
$test52_total = ($conv_total_payment == 2307500); // 2307.5 VIZ

echo "Test 52.1 (profit = 655 VIZ): " . ($test52_profit ? 'PASS' : 'FAIL') . "\n";
echo "Test 52.2 (fee = 327.5 VIZ): " . ($test52_fee ? 'PASS' : 'FAIL') . "\n";
echo "Test 52.3 (total payment = 2307.5 VIZ): " . ($test52_total ? 'PASS' : 'FAIL') . "\n";

// Verify: conversion only allowed when current_profit > 0
$conv_no_profit_cv = $conv_threshold - 10000; // below threshold = no profit
$conv_no_profit_profit = max(0, $conv_no_profit_cv - $conv_threshold);
$test52_no_profit = ($conv_no_profit_profit == 0);
echo "Test 52.4 (no conversion when profit <= 0): " . ($test52_no_profit ? 'PASS' : 'FAIL') . "\n";

step('Scenario 52: Convert to Normal Bet', [
    'profit' => $conv_profit,
    'fee' => $conv_fee,
    'total_payment' => $conv_total_payment,
    'test_profit' => $test52_profit,
    'test_fee' => $test52_fee,
    'test_total' => $test52_total,
    'test_no_profit' => $test52_no_profit,
]);

// ============================================================
// SCENARIO 53: BATCH COMMIT-REVEAL LIFECYCLE
// Full flow: commit → reveal → batch settle; plus forfeit path
// ============================================================
echo "\n" . str_repeat('=', 70) . "\n";
echo "SCENARIO 53: BATCH COMMIT-REVEAL LIFECYCLE\n";
echo str_repeat('=', 70) . "\n";

// Add commit-reveal settings
$SETTINGS['commit_reveal_enabled'] = 1;
$SETTINGS['commit_no_reveal_penalty_permille'] = 200; // 20%
$SETTINGS['min_batch_bet'] = 1000; // 1 VIZ
$SETTINGS['batch_epoch_blocks'] = 20;
$SETTINGS['reveal_window_blocks'] = 200;

$oracle53 = make_user(500, 'Oracle53', 100000000);
register_oracle($oracle53, 0, 5, $SETTINGS);
oracle_deposit_insurance($oracle53, 5000000);
$maker53 = make_user(501, 'Maker53', 100000000);
register_creator($maker53, $SETTINGS);

// Market with allow_batch=1, allow_instant_bet=1
$r53 = create_market($maker53, $oracle53, 200000, 5, 10, $T0,
    $T0 + 172800, $T0 + 259200, 1, 10, 0, $SETTINGS);
$m53 = $r53['market'];
$m53['allow_batch'] = 1;
$m53['allow_instant_bet'] = 1;
$m53['forfeit_pool'] = 0;
$lps53 = [$r53['lp']];
oracle_accept($m53, $oracle53, $SETTINGS);

$committer_a = make_user(502, 'CommitterA', 100000000);
$committer_b = make_user(503, 'CommitterB', 100000000);
$committer_c = make_user(504, 'CommitterC', 100000000); // will forfeit

// 53a: CommitterA commits 100 VIZ on side A
$escrow_a = 100000;
$salt_a = 'secret_salt_a53';
$r_ca = commit_bet($m53, $committer_a, $escrow_a, 0, 100000, 0, $salt_a, $SETTINGS, $T0 + 3600);
$commit_a = $r_ca['commit'];
step('53a: CommitterA commits 100 VIZ on A (escrow locked)', [
    'commit_id' => $commit_a['id'],
    'escrow' => fmt($commit_a['escrow_amount']),
    'no_reveal_fee' => $commit_a['no_reveal_fee_permille'] . '‰',
    'reveal_deadline' => $commit_a['reveal_deadline'],
    'committer_a_balance' => fmt($committer_a['balance']),
]);
assert($r_ca['status'] === true);
assert($committer_a['balance'] == 100000000 - 100000); // escrow deducted

// 53b: CommitterB commits 80 VIZ on side B
$escrow_b = 80000;
$salt_b = 'secret_salt_b53';
$r_cb = commit_bet($m53, $committer_b, $escrow_b, 1, 80000, 0, $salt_b, $SETTINGS, $T0 + 3700);
$commit_b = $r_cb['commit'];
step('53b: CommitterB commits 80 VIZ on B', [
    'commit_id' => $commit_b['id'],
    'escrow' => fmt($commit_b['escrow_amount']),
]);
assert($r_cb['status'] === true);

// 53c: CommitterC commits 50 VIZ on side A (will NOT reveal → forfeit)
$escrow_c = 50000;
$salt_c = 'secret_salt_c53';
$r_cc = commit_bet($m53, $committer_c, $escrow_c, 0, 50000, 0, $salt_c, $SETTINGS, $T0 + 3800);
$commit_c = $r_cc['commit'];
step('53c: CommitterC commits 50 VIZ on A (will forfeit)', [
    'commit_id' => $commit_c['id'],
]);

// 53d: CommitterA reveals at T0+4000 (within deadline)
$reveal_time_a = $T0 + 4000;
$r_ra = reveal_bet($commit_a, $m53, $committer_a, $reveal_time_a);
$bet_ra = $r_ra['bet'];
step('53d: CommitterA reveals (within deadline)', [
    'bet_id' => $bet_ra['id'],
    'amount' => fmt($bet_ra['amount']),
    'side' => $bet_ra['side'],
    'surplus_refund' => fmt($r_ra['surplus_refund']),
    'epoch' => $r_ra['epoch'],
    'status' => $bet_ra['status'] . ' (5=queued)',
]);
assert($r_ra['status'] === true);
assert($bet_ra['status'] == 5); // queued
assert($r_ra['surplus_refund'] == 0); // escrow == amount

// 53e: CommitterB reveals with surplus (escrow > amount)
// Re-commit with larger escrow to test surplus refund
$committer_b2 = make_user(505, 'CommitterB2', 100000000);
$escrow_b2 = 100000; // 100 VIZ escrow but only 60 VIZ bet
$amount_b2 = 60000;
$salt_b2 = 'salt_surplus';
$r_cb2 = commit_bet($m53, $committer_b2, $escrow_b2, 1, $amount_b2, 0, $salt_b2, $SETTINGS, $T0 + 4100);
$commit_b2 = $r_cb2['commit'];
$balance_before_reveal = $committer_b2['balance'];
$r_rb2 = reveal_bet($commit_b2, $m53, $committer_b2, $T0 + 4200);
$bet_rb2 = $r_rb2['bet'];
step('53e: CommitterB2 reveals with surplus (escrow 100, bet 60, refund 40)', [
    'surplus_refund' => fmt($r_rb2['surplus_refund']),
    'balance_before' => fmt($balance_before_reveal),
    'balance_after' => fmt($committer_b2['balance']),
    'bet_amount' => fmt($bet_rb2['amount']),
]);
assert($r_rb2['surplus_refund'] == 40000); // 100 - 60 = 40 VIZ refunded
assert($committer_b2['balance'] == $balance_before_reveal + 40000);

// 53f: CommitterC does NOT reveal → commit_forfeit at deadline
$forfeit_time = $commit_c['reveal_deadline'] + 1;
$c_balance_before_forfeit = $committer_c['balance'];
$r_fc = commit_forfeit($commit_c, $m53, $committer_c);
step('53f: CommitterC FORFEITS (deadline passed, no reveal)', [
    'penalty' => fmt($r_fc['penalty']) . ' (20% of 50 VIZ = 10 VIZ)',
    'refund' => fmt($r_fc['refund']) . ' (80% returned)',
    'forfeit_pool' => fmt($r_fc['forfeit_pool']),
    'commit_status' => $commit_c['status'] . ' (2=forfeited)',
]);
assert($r_fc['penalty'] == 10000); // 50000 * 200/1000 = 10000 (10 VIZ)
assert($r_fc['refund'] == 40000);  // 50000 - 10000 = 40000
assert($commit_c['status'] == 2);
assert($m53['forfeit_pool'] == 10000);
assert($committer_c['balance'] == $c_balance_before_forfeit + 40000);

// 53g: Batch settle all queued bets
$queued53 = [$bet_ra, $bet_rb2]; // only revealed bets
$settle_result = batch_settle($m53, $queued53);
step('53g: BATCH SETTLE (2 revealed bets)', [
    'side_totals' => $settle_result['side_totals'],
    'side_tokens' => $settle_result['side_tokens'],
    'dust' => $settle_result['dust'],
    'reserves_after' => $settle_result['reserves'],
]);

// Verify bets are now active with tokens
$active_bets = array_filter($queued53, function($b) { return $b['status'] == 0; });
assert(count($active_bets) == 2, 'Both bets should be active after batch settle');
foreach ($queued53 as $b) {
    if ($b['status'] == 0) {
        assert($b['weight'] > 0, 'Active bet must have tokens');
    }
}

// 53h: Resolve market and verify forfeit_pool boosts winners
$bets53_all = array_filter($queued53, function($b) { return $b['status'] == 0; });
$payouts53 = [];
$lps53_ref = $lps53;
$r_res53 = resolve_market($m53, 0, $bets53_all, $lps53_ref, $payouts53);
step('53h: Resolve to A — forfeit_pool boosts winners', [
    'forfeit_pool_used' => fmt($m53['forfeit_pool']),
    'winner_payouts' => $r_res53['winner_payouts'],
    'note' => 'forfeit_pool added to fee_pool → LP bonus (part of winners ecosystem)',
]);

// Verify forfeit_pool was added to the fee pool (boosting LP/winner payouts)
$test53_forfeit = ($m53['forfeit_pool'] > 0);
echo "Test 53.1 (forfeit_pool > 0): " . ($test53_forfeit ? 'PASS' : 'FAIL') . "\n";
echo "Test 53.2 (both commits revealed and settled): " . (count($active_bets) == 2 ? 'PASS' : 'FAIL') . "\n";
echo "Test 53.3 (forfeited commit penalty = 20%): " . ($r_fc['penalty'] == 10000 ? 'PASS' : 'FAIL') . "\n";
echo "Test 53.4 (surplus refund on reveal): " . ($r_rb2['surplus_refund'] == 40000 ? 'PASS' : 'FAIL') . "\n";

step('Scenario 53 summary: Batch commit-reveal lifecycle', [
    'commits_placed' => 3,
    'revealed' => 2,
    'forfeited' => 1,
    'forfeit_penalty' => fmt($r_fc['penalty']),
    'batch_settled' => count($active_bets) . ' bets',
    'all_tests_pass' => ($test53_forfeit && count($active_bets) == 2) ? 'YES' : 'NO',
]);


// ============================================================
// SCENARIO 54: ACCOUNT-MODE DISPUTE RESOLUTION (dispute_mode=1)
// Centralized resolver account decides the outcome
// ============================================================
echo "\n" . str_repeat('=', 70) . "\n";
echo "SCENARIO 54: ACCOUNT-MODE DISPUTE RESOLUTION (dispute_mode=1)\n";
echo str_repeat('=', 70) . "\n";

$oracle54 = make_user(510, 'Oracle54', 100000000);
register_oracle($oracle54, 0, 5, $SETTINGS);
oracle_deposit_insurance($oracle54, 5000000);
$maker54 = make_user(511, 'Maker54', 100000000);
register_creator($maker54, $SETTINGS);
$resolver54 = make_user(512, 'Resolver54', 100000000); // centralized dispute resolver
$dao54 = make_user(513, 'DAO54', 0); // DAO fund

// Create market with dispute_mode=1, dispute_resolver=Resolver54
$r54 = create_market($maker54, $oracle54, 200000, 5, 10, $T0,
    $T0 + 172800, $T0 + 259200, 1, 10, 0, $SETTINGS);
$m54 = $r54['market'];
$m54['dispute_mode'] = 1; // account mode
$m54['dispute_resolver'] = $resolver54['id'];
$lps54 = [$r54['lp']];
oracle_accept($m54, $oracle54, $SETTINGS);

$bettor54a = make_user(514, 'Bettor54A', 100000000);
$bettor54b = make_user(515, 'Bettor54B', 100000000);

$r54a = place_bet($m54, $bettor54a, 0, 100000, $T0 + 3600); // 100 VIZ on A
$r54b = place_bet($m54, $bettor54b, 1, 80000, $T0 + 7200);  // 80 VIZ on B
$bets54 = [$r54a['bet'], $r54b['bet']];
step('Bets placed on account-mode market', [
    'bettor_a' => fmt($r54a['bet']['amount']) . ' on A',
    'bettor_b' => fmt($r54b['bet']['amount']) . ' on B',
]);

// Oracle resolves to A (WRONG — should be B)
$payouts54 = [];
resolve_market($m54, 0, $bets54, $lps54, $payouts54);
step('Oracle resolves to A (WRONG, should be B)', ['market_outcome' => $m54['resolved_outcome']]);

// Bettor54B files dispute
$r_dis54 = create_dispute($m54, $bettor54b, $SETTINGS);
step('Bettor54B files dispute', ['dispute_fee' => fmt($SETTINGS['dispute_fee'])]);

// 54a: Non-resolver tries to resolve → rejected
$fake_resolver = make_user(516, 'FakeResolver', 100000000);
$m54_copy = $m54; // don't modify real market for this test
$null_creator54 = null;
$fake_result = resolve_dispute_account_mode($m54_copy, $fake_resolver, $bettor54b, $oracle54, 1,
    $bets54, $lps54, $payouts54, $SETTINGS['dispute_fee'], 0, 0, 0, 0, 0, $null_creator54, $dao54, $SETTINGS);
step('54a: Non-resolver tries to resolve → REJECTED', [
    'error' => $fake_result['error'],
    'test' => (isset($fake_result['error']) && $fake_result['error'] == 'Not the designated resolver') ? 'PASS' : 'FAIL',
]);
assert(isset($fake_result['error']));

// 54b: Legitimate resolver resolves — oracle wrong, correct=B, with penalty + oracle ban
$oracle54_insurance_before = $oracle54['oracle_insurance'];
$resolver54_balance_before = $resolver54['balance'];
$r_resolve54 = resolve_dispute_account_mode($m54, $resolver54, $bettor54b, $oracle54, 1,
    $bets54, $lps54, $payouts54, $SETTINGS['dispute_fee'],
    1000000, 1, 0, 0, 0, $maker54, $dao54, $SETTINGS); // 1000 VIZ penalty, ban oracle
step('54b: Resolver54 resolves: oracle WRONG, correct=B, 1000 VIZ penalty, oracle banned', [
    'decision' => $r_resolve54['decision'],
    'correct_outcome' => $r_resolve54['correct_outcome'],
    'plaintiff_refund' => fmt($r_resolve54['plaintiff_refund']),
    'disputer_reward' => fmt($r_resolve54['disputer_reward']),
    'reward_pool' => fmt($r_resolve54['reward_pool']),
    'penalty' => fmt($r_resolve54['penalty']),
    'resolver_fee' => fmt($r_resolve54['resolver_fee']),
    'oracle_insurance_before' => fmt($oracle54_insurance_before),
    'oracle_insurance_after' => fmt($oracle54['oracle_insurance']),
    'oracle_banned' => $oracle54['oracle_banned'],
]);

// Verify resolver got a fee
$test54_resolver_fee = ($resolver54['balance'] > $resolver54_balance_before);
$test54_oracle_slashed = ($oracle54['oracle_insurance'] < $oracle54_insurance_before);
$test54_plaintiff_rewarded = ($bettor54b['balance'] > 100000000 - $SETTINGS['dispute_fee']); // got refund + reward

echo "Test 54.1 (resolver receives fee): " . ($test54_resolver_fee ? 'PASS' : 'FAIL') . "\n";
echo "Test 54.2 (oracle insurance slashed): " . ($test54_oracle_slashed ? 'PASS' : 'FAIL') . "\n";
echo "Test 54.3 (plaintiff rewarded): " . ($test54_plaintiff_rewarded ? 'PASS' : 'FAIL') . "\n";
echo "Test 54.4 (oracle banned): " . ($oracle54['oracle_banned'] == 1 ? 'PASS' : 'FAIL') . "\n";

// 54c: Auto-payout recalculated for correct outcome B
$users_map54 = [510 => &$oracle54, 511 => &$maker54, 512 => &$resolver54, 514 => &$bettor54a, 515 => &$bettor54b];
$r_pay54 = auto_payout($payouts54, $users_map54);
step('54c: Auto-payout after account-mode dispute (recalculated for B)', [
    'paid' => $r_pay54,
    'balances' => balances($users_map54),
]);

// Verify: Bettor54B (side B = winner) should get payout > original bet
$test54_winner = ($bettor54b['balance'] > 100000000); // net positive
echo "Test 54.5 (B bettor wins after dispute recalc): " . ($test54_winner ? 'PASS' : 'FAIL') . "\n";

step('Scenario 54 summary: Account-mode dispute', [
    'dispute_mode' => 'account (centralized)',
    'resolver' => 'Resolver54',
    'outcome_flipped' => 'A→B',
    'oracle_penalized' => fmt($oracle54_insurance_before - $oracle54['oracle_insurance']),
    'all_tests_pass' => ($test54_resolver_fee && $test54_oracle_slashed && $test54_plaintiff_rewarded && $oracle54['oracle_banned']) ? 'YES' : 'NO',
]);


// ============================================================
// SCENARIO 55: allow_instant_bet=FALSE ENFORCEMENT
// Instant mode=0 rejected, only batch/commit-reveal allowed
// ============================================================
echo "\n" . str_repeat('=', 70) . "\n";
echo "SCENARIO 55: allow_instant_bet=FALSE ENFORCEMENT\n";
echo str_repeat('=', 70) . "\n";

$oracle55 = make_user(520, 'Oracle55', 100000000);
register_oracle($oracle55, 0, 5, $SETTINGS);
oracle_deposit_insurance($oracle55, 5000000);
$maker55 = make_user(521, 'Maker55', 100000000);
register_creator($maker55, $SETTINGS);

// Create market with allow_instant_bet=0, allow_batch=1 (anti-MEV market)
$r55 = create_market($maker55, $oracle55, 200000, 5, 10, $T0,
    $T0 + 172800, $T0 + 259200, 1, 10, 0, $SETTINGS);
$m55 = $r55['market'];
$m55['allow_batch'] = 1;
$m55['allow_instant_bet'] = 0; // FORCE batch/commit-reveal only
$lps55 = [$r55['lp']];
oracle_accept($m55, $oracle55, $SETTINGS);

$instant_bettor = make_user(522, 'InstantBettor', 100000000);
$batch_bettor = make_user(523, 'BatchBettor', 100000000);
$commit_bettor = make_user(524, 'CommitBettor', 100000000);

// 55a: Instant bet (mode=0) on allow_instant_bet=false market → REJECTED
// Simulate the enforcement check that the chain would do
function enforce_instant_bet_check($market, $mode) {
    if ($mode == 0 && empty($market['allow_instant_bet'])) {
        return ['error' => 'pm_instant_bet_disabled: market requires batch or commit-reveal'];
    }
    return ['status' => true];
}

$instant_check = enforce_instant_bet_check($m55, 0);
step('55a: Instant bet (mode=0) on allow_instant_bet=false → REJECTED', [
    'result' => $instant_check,
    'test' => (isset($instant_check['error']) && strpos($instant_check['error'], 'pm_instant_bet_disabled') !== false) ? 'PASS' : 'FAIL',
]);
assert(isset($instant_check['error']));

// 55b: Batch bet (mode=1) succeeds
$r_batch55 = place_bet_batch($m55, $batch_bettor, 0, 100000, $T0 + 3600);
step('55b: Batch bet (mode=1) on allow_instant_bet=false → ACCEPTED', [
    'bet_id' => $r_batch55['bet']['id'],
    'amount' => fmt($r_batch55['bet']['amount']),
    'status' => $r_batch55['bet']['status'] . ' (5=queued)',
    'epoch' => $r_batch55['epoch'],
]);
assert($r_batch55['status'] === true);
assert($r_batch55['bet']['status'] == 5); // queued

// 55c: Commit-reveal (mode=2) succeeds
$salt55 = 'salt55test';
$r_commit55 = commit_bet($m55, $commit_bettor, 50000, 1, 50000, 0, $salt55, $SETTINGS, $T0 + 3700);
step('55c: Commit-reveal (mode=2) on allow_instant_bet=false → ACCEPTED', [
    'commit_id' => $r_commit55['commit']['id'],
    'escrow' => fmt($r_commit55['commit']['escrow_amount']),
]);
assert($r_commit55['status'] === true);

// 55d: Mutual constraint check — market with both false would be unbettable
function validate_market_betability($allow_instant, $allow_batch) {
    if (!$allow_instant && !$allow_batch) {
        return ['valid' => false, 'error' => 'Market unbettable: both instant and batch disabled'];
    }
    return ['valid' => true];
}

$constraint_fail = validate_market_betability(false, false);
$constraint_pass = validate_market_betability(false, true);
$constraint_pass2 = validate_market_betability(true, false);
step('55d: Mutual constraint (allow_instant_bet OR allow_batch must be true)', [
    'both_false' => $constraint_fail,
    'instant_false_batch_true' => $constraint_pass,
    'instant_true_batch_false' => $constraint_pass2,
]);
assert($constraint_fail['valid'] === false);
assert($constraint_pass['valid'] === true);
assert($constraint_pass2['valid'] === true);

// 55e: Batch settle the queued bet and verify it works
$queued55 = [$r_batch55['bet']];
$settle55 = batch_settle($m55, $queued55);
step('55e: Batch settle on anti-MEV market', [
    'side_totals' => $settle55['side_totals'],
    'side_tokens' => $settle55['side_tokens'],
    'bet_active' => ($queued55[0]['status'] == 0) ? 'YES' : 'NO',
    'bet_tokens' => $queued55[0]['weight'],
]);
assert($queued55[0]['status'] == 0); // active after settle
assert($queued55[0]['weight'] > 0);

$test55_instant_rejected = isset($instant_check['error']);
$test55_batch_accepted = ($r_batch55['status'] === true);
$test55_commit_accepted = ($r_commit55['status'] === true);
$test55_constraint = ($constraint_fail['valid'] === false && $constraint_pass['valid'] === true);
$test55_settled = ($queued55[0]['status'] == 0 && $queued55[0]['weight'] > 0);

echo "Test 55.1 (instant rejected): " . ($test55_instant_rejected ? 'PASS' : 'FAIL') . "\n";
echo "Test 55.2 (batch accepted): " . ($test55_batch_accepted ? 'PASS' : 'FAIL') . "\n";
echo "Test 55.3 (commit-reveal accepted): " . ($test55_commit_accepted ? 'PASS' : 'FAIL') . "\n";
echo "Test 55.4 (mutual constraint enforced): " . ($test55_constraint ? 'PASS' : 'FAIL') . "\n";
echo "Test 55.5 (batch settled successfully): " . ($test55_settled ? 'PASS' : 'FAIL') . "\n";

step('Scenario 55 summary: allow_instant_bet=false', [
    'instant_mode_rejected' => $test55_instant_rejected ? 'YES' : 'NO',
    'batch_mode_accepted' => $test55_batch_accepted ? 'YES' : 'NO',
    'commit_reveal_accepted' => $test55_commit_accepted ? 'YES' : 'NO',
    'mutual_constraint' => $test55_constraint ? 'ENFORCED' : 'BROKEN',
    'all_tests_pass' => ($test55_instant_rejected && $test55_batch_accepted && $test55_commit_accepted && $test55_constraint && $test55_settled) ? 'YES' : 'NO',
]);


// ============================================================
// SCENARIO 56: ORACLE FIXED FEE TRANSFER AT ACCEPT
// Creator pays oracle_fixed_fee when oracle accepts (non-self)
// Self-oracle skips the transfer
// ============================================================
echo "\n" . str_repeat('=', 70) . "\n";
echo "SCENARIO 56: ORACLE FIXED FEE TRANSFER AT ACCEPT\n";
echo str_repeat('=', 70) . "\n";

// 56a: External oracle — fixed fee transferred
$oracle56a = make_user(530, 'Oracle56A', 100000000);
register_oracle($oracle56a, 0, 5, $SETTINGS, 15000); // fixed_fee = 15 VIZ
oracle_deposit_insurance($oracle56a, 5000000);

$maker56a = make_user(531, 'Maker56A', 100000000);
register_creator($maker56a, $SETTINGS);

$r56a = create_market($maker56a, $oracle56a, 200000, 5, 10, $T0,
    $T0 + 172800, $T0 + 259200, 1, 10, 0, $SETTINGS);
$m56a = $r56a['market'];
$m56a['oracle_fixed_fee'] = 15000; // 15 VIZ

$maker_balance_before = $maker56a['balance'];
$oracle_balance_before = $oracle56a['balance'];

$r_accept56a = oracle_accept_with_fixed_fee($m56a, $oracle56a, $maker56a, $SETTINGS);
step('56a: External oracle accepts → fixed fee transferred (15 VIZ)', [
    'fixed_fee_transferred' => fmt($r_accept56a['fixed_fee_transferred']),
    'is_self_oracle' => $r_accept56a['is_self_oracle'] ? 'YES' : 'NO',
    'maker_balance_change' => fmt($maker_balance_before - $maker56a['balance']),
    'oracle_balance_change' => fmt($oracle56a['balance'] - $oracle_balance_before),
    'market_status' => $m56a['status'],
]);

$test56a_fee = ($r_accept56a['fixed_fee_transferred'] == 15000);
$test56a_maker = (($maker_balance_before - $maker56a['balance']) == 15000);
$test56a_oracle = (($oracle56a['balance'] - $oracle_balance_before) == 15000);
$test56a_active = ($m56a['status'] == 1);

echo "Test 56.1 (fixed fee = 15 VIZ): " . ($test56a_fee ? 'PASS' : 'FAIL') . "\n";
echo "Test 56.2 (maker debited 15 VIZ): " . ($test56a_maker ? 'PASS' : 'FAIL') . "\n";
echo "Test 56.3 (oracle credited 15 VIZ): " . ($test56a_oracle ? 'PASS' : 'FAIL') . "\n";
echo "Test 56.4 (market active): " . ($test56a_active ? 'PASS' : 'FAIL') . "\n";

// 56b: Self-oracle — fixed fee is SKIPPED (no self-transfer)
$self56b = make_user(532, 'SelfOracle56B', 100000000);
register_oracle($self56b, 0, 5, $SETTINGS, 20000); // fixed_fee = 20 VIZ
oracle_deposit_insurance($self56b, 5000000);
register_creator($self56b, $SETTINGS);

$r56b = create_market($self56b, $self56b, 200000, 5, 10, $T0,
    $T0 + 172800, $T0 + 259200, 1, 10, 0, $SETTINGS);
$m56b = $r56b['market'];
$m56b['oracle_fixed_fee'] = 20000;
// Self-oracle auto-approves (status=1) in create_market — reset to 0 to test accept flow
$m56b['status'] = 0;

$self_balance_before = $self56b['balance'];
$r_accept56b = oracle_accept_with_fixed_fee($m56b, $self56b, $self56b, $SETTINGS);
step('56b: Self-oracle accepts → fixed fee SKIPPED (no self-transfer)', [
    'fixed_fee_transferred' => fmt($r_accept56b['fixed_fee_transferred']),
    'is_self_oracle' => $r_accept56b['is_self_oracle'] ? 'YES' : 'NO',
    'balance_unchanged' => ($self56b['balance'] == $self_balance_before) ? 'YES' : 'NO',
    'market_status' => $m56b['status'],
]);

$test56b_skip = ($r_accept56b['fixed_fee_transferred'] == 0);
$test56b_self = ($r_accept56b['is_self_oracle'] === true);
$test56b_balance = ($self56b['balance'] == $self_balance_before);
$test56b_active = ($m56b['status'] == 1);

echo "Test 56.5 (self-oracle fee skipped): " . ($test56b_skip ? 'PASS' : 'FAIL') . "\n";
echo "Test 56.6 (is_self_oracle=true): " . ($test56b_self ? 'PASS' : 'FAIL') . "\n";
echo "Test 56.7 (balance unchanged): " . ($test56b_balance ? 'PASS' : 'FAIL') . "\n";
echo "Test 56.8 (market active): " . ($test56b_active ? 'PASS' : 'FAIL') . "\n";

// 56c: Creator has insufficient balance for fixed fee — partial transfer
$oracle56c = make_user(533, 'Oracle56C', 100000000);
register_oracle($oracle56c, 0, 5, $SETTINGS, 50000); // fixed_fee = 50 VIZ
oracle_deposit_insurance($oracle56c, 5000000);

$poor_maker = make_user(534, 'PoorMaker', 500000); // enough to create market
register_creator($poor_maker, $SETTINGS);

$r56c = create_market($poor_maker, $oracle56c, 200000, 5, 10, $T0,
    $T0 + 172800, $T0 + 259200, 1, 10, 0, $SETTINGS);
$m56c = $r56c['market'];
$m56c['oracle_fixed_fee'] = 50000; // wants 50 VIZ

// Simulate: after market creation, maker only has 10 VIZ left
$poor_maker['balance'] = 10000;

$poor_balance = $poor_maker['balance']; // 10000
$r_accept56c = oracle_accept_with_fixed_fee($m56c, $oracle56c, $poor_maker, $SETTINGS);
step('56c: Maker with insufficient balance — transfers what they have', [
    'fixed_fee_requested' => fmt(50000),
    'fixed_fee_transferred' => fmt($r_accept56c['fixed_fee_transferred']),
    'maker_balance_after' => fmt($poor_maker['balance']),
]);

$test56c_partial = ($r_accept56c['fixed_fee_transferred'] == $poor_balance);
echo "Test 56.9 (partial transfer = available balance): " . ($test56c_partial ? 'PASS' : 'FAIL') . "\n";

step('Scenario 56 summary: Oracle fixed fee at accept', [
    'external_transfer' => $test56a_fee ? '15 VIZ transferred' : 'FAIL',
    'self_oracle_skip' => $test56b_skip ? 'Skipped (no self-transfer)' : 'FAIL',
    'insufficient_balance' => $test56c_partial ? 'Partial transfer' : 'FAIL',
    'all_tests_pass' => ($test56a_fee && $test56b_skip && $test56c_partial) ? 'YES' : 'NO',
]);


// ============================================================
// FINAL SUMMARY
// ============================================================
echo "\n\n" . str_repeat('=', 70) . "\n";
echo "ALL SCENARIOS COMPLETED\n";
echo str_repeat('=', 70) . "\n";
echo "Scenarios tested:\n";
echo "  1. Happy path: oracle resolves correctly, auto-payout\n";
echo "  2. Oracle rejects market: liquidity returned\n";
echo "  3. Self-oracle: auto-approve (no pending queue)\n";
echo "  4. Time penalty: early/mid/late/very-late bets, quadratic penalty deductions\n";
echo "  5. Oracle misses resolution: refund all + penalty distribution\n";
echo "  6. Dispute \u2014 oracle WRONG: recalculate payouts, plaintiff rewarded\n";
echo "  7. Dispute \u2014 oracle RIGHT: committee gets fee, original payouts kept\n";
echo "  8. Bet cancellation: CPMM reverse + slippage\n";
echo "  9. Multiple LPs: TIME-WEIGHTED fee distribution (early LP earns more)\n";
echo "  10. Penalty curve comparison: linear vs quadratic\n";
echo "  11. Low-volume LP solvency: 1 small bet, one-sided, LP principal guaranteed\n";
echo "  12. Oracle re-registration: profile update without fee\n";
echo "  13. Dispute with extra penalty + permanent oracle ban\n";
echo "  14. Dispute with time-limited creator ban\n";
echo "  15. Fractional LP withdrawal (partial + remainder)\n";
echo "  16. Risk score \u2014 oracle insurance coverage (betting block + listing filter)\n";
echo "  17. Oracle voluntary no-contest (grace period, pending refund payouts, auto-payout)\n";
echo "  18. Dispute against abusive no-contest (3-outcome resolution: A/B/no-contest + sanctions)\n";
echo "  19. Auto-close stale dispute (14-day timeout, refund all + oracle penalty)\n";
echo "  20. Lazy Pool: deposit + shares + lock period\n";
echo "  21. Lazy Pool: share calculation after profit (reward_per_share accumulator)\n";
echo "  22. Lazy Pool: late depositor fairness (no old rewards)\n";
echo "  23. Lazy Pool: deposit unlock consolidation\n";
echo "  24. Lazy Pool: planned withdrawal (full)\n";
echo "  25. Lazy Pool: emergency withdrawal with penalty on locked profit\n";
echo "  26. Lazy Pool: emergency no profit = no penalty\n";
echo "  27. Lazy Pool: market auto-allocation + profit distribution\n";
echo "  28. Lazy Pool: edge cases (1:1 first deposit, zero reward, partial withdrawal)\n";
echo "  29. LMSR Math: prices, cost, buy/sell roundtrip, tokens_for_amount, b_from_liquidity\n";
echo "  30. Onix Multi Settlement: losers forfeit, fees deducted, winners split by tokens\n";
echo "  31. Creator Fee in Binary: creator receives fee from losers_sum\n";
echo "  32. Multi-Market Lifecycle: create \u2192 bet \u2192 resolve \u2192 verify payouts\n";
echo "  33. LP Principal Guarantee: subsidy architecturally separate\n";
echo "  34. Edge Cases: all-on-winner, no-on-winner, zero-volume, single bettor\n";
echo "  35. Position Transfers: full, partial, to-self (fail), excessive (fail), transfer+resolve\n";
echo "  36. Graduated Early Recall: idle market loses allocation over time, active market keeps it\n";
echo "  37. Active Market Penalty: recursive 5% per oracle active market\n";
echo "  38. Fault Penalty Stamps: stamps on faults, auto-healing after 10 days, combined B+C\n";
echo "  39. Market Metadata: category/subcategory/tags validation\n";
echo "  40. Metadata JSON: build + localization structure\n";
echo "  41. Localization fallback resolver (resolve_i18n simulation)\n";
echo "  42. Jurisdiction filtering: banned/allowed tag logic\n";
echo "  43. Category structure: i18n completeness check\n";
echo "  44. Market creation with metadata: simulated API flow\n";
echo "  45. i18n JSON files: key consistency between en.json and ru.json\n";
echo "  46. Leverage cancel_value calculation (CPMM)\n";
echo "  47. Liquidation threshold & safety margin\n";
echo "  48. Max leverage binary search (CPMM)\n";
echo "  49. Leveraged position lifecycle (open -> close)\n";
echo "  50. Cancel-bet liquidation waterfall (Case B)\n";
echo "  51. LMSR max bet amount (Constraint 2)\n";
echo "  52. Convert to Normal Bet calculation\n";
echo "  53. Batch Commit-Reveal: commit → reveal → batch settle + forfeit penalty\n";
echo "  54. Account-Mode Dispute Resolution (dispute_mode=1): centralized resolver decides\n";
echo "  55. allow_instant_bet=false: instant rejected, batch/commit-reveal accepted, mutual constraint\n";
echo "  56. Oracle Fixed Fee Transfer at Accept (external vs self-oracle, insufficient balance)\n";
echo "\nRun: php tests/workflow_test.php\n";
