# Club management

Club windows share the same session actions as the graphical client. Read a projection, choose current identifiers, apply an action and inspect its result before using a stale row or price again.

## Squad, tactics and training

`Lineup` lists squad members and positions; use explicit player references for moves and orders. The engine checks availability, formation and substitution rules. `Tactics` changes preparation values, while automation flags can delegate selected decisions. `Training` has types, areas, intensity and focus; opening or initialising a plan is an action distinct from merely reading it. A legal starting eleven takes priority over speculative transfers.

## Transfers and contracts

`Transfer` provides market filters, players and purchase/loan actions. `HirePlayer` requests an ordinary loan for a supported duration; free agents must be bought rather than borrowed. `Transfer.Negotiate` holds a staged offer: change fields with `SetOffer` or `SetOfferField`, submit with `SubmitOffer`, or cancel through `GoBack`. The engine applies qualification, squad capacity, wages, fees and acceptance rules; an opened editor is not an accepted purchase.

A loan expiry can suspend calendar processing for a return question addressed to the borrowing manager. The manager chooses the displayed No or Yes option. Yes opens the normal staged buyback negotiation; cancelling that editor follows the ordinary return path. Only the owning manager can edit or close it. Do not start another calendar operation while a modal question or editor owns the suspended operation.

`Contracts.Negotiate` is a separate contract editor. Player IDs remain distinct from squad positions and temporary offer rows. Refresh demand and offer views after changes; neither a displayed demand nor a pending offer guarantees final acceptance.

## Finance and facilities

`Bank`, `Advertising`, `Stadium`, `Toto` and related subwindows expose normal financial actions. Quotes and offer rows belong to their current projection. Advertising slots can be empty, active or require renewal; reading a general budget does not itself secure a sponsor. Training camps use edition-provided resort tables and the displayed currency conversion. Read the actual quote instead of hardcoding a price from another edition.

Loans, deposits, shares, facilities and campaigns all have rule and budget checks. A cash balance alone does not establish affordability when wages, credit or a pending obligation also matter.

## Command index

This index includes registered and reserved names. `ListOptions` and `Help` from the running build determine current signatures and availability. An indexed command may reject an unsupported context with `E_ENGINE_GAP`. Parameters retain the console notation; `—` means none.

### `Lineup`

| Command | Parameters | Purpose |
|---|---|---|
| `ShowSquad` | [view:enum(A,B)] | the squad: values, strength, place |
| `SetSquadView` | view:enum(A,B) | DATEN-A / DATEN-B |
| `SwapPlayers` | first:Player, second:Player | exchange two players' places |
| `OpenPlayer` | player:Player | the player card (PlayerCard) |
| `OpenPitch` | – | the pitch (Lineup.Pitch) |
| `OpenTeamList` | – | Open Team List. See current Help for behaviour and bounds. |
| `ShowForeigners` | – | Show Foreigners. See current Help for behaviour and bounds. |

### `Lineup.Pitch`

| Command | Parameters | Purpose |
|---|---|---|
| `ShowPitch` | – | the eleven with depth/lane, formations, automatic weights |
| `SetFormation` | slot:int(1..8) | one of the eight formations (1-4 given, 5-8 own); the automatic line-up takes over |
| `SaveFormation` | slot:int(5..8) | save the current positions in an own formation slot (long press) |
| `MovePlayer` | player:Player, depth:int, lane:int | place a starter freely (switches the automatic line-up off) |
| `SwapPitch` | first:Player, second:Player | exchange two players on the pitch / bench |
| `SetAutoWeights` | kind:enum(Position,SideAndRole,Fatigue), value:int(0..3) | an automatic line-up weight (the cup is clicked until the value); then SetFormation again |
| `ShowAutoWeights` | – | the three automatic line-up weights |
| `ScrollList` | direction:enum(Up,Down) | Scroll List. See current Help for behaviour and bounds. |
| `SetSquadView` | view:enum(A,B) | DATEN-A / DATEN-B |
| `OpenTeamList` | – | Open Team List. See current Help for behaviour and bounds. |
| `OpenOrder` | – | ORDER: the instructions of the starters (Lineup.Order) |
| `OpenMarking` | – | Open Marking. See current Help for behaviour and bounds. |
| `ShowZoneCoverage` | – | Show Zone Coverage. See current Help for behaviour and bounds. |
| `OpenSetPieces` | – | Open Set Pieces. See current Help for behaviour and bounds. |
| `ShowPairs` | – | the Hall partners stored on the tactics board |
| `PairPlayers` | first:Player, second:Player, depth:int(0..19), lane:int(0..8) | place the first Hall starter beside the second at the explicit target coordinates |

### `Lineup.Order`

| Command | Parameters | Purpose |
|---|---|---|
| `ShowOrders` | – | offense, running, commitment, fighting of every starter |
| `SetOrder` | player:Player, aspect:enum(Offense,Run,Commitment,Fighting), value:int | one instruction of a starter (as a click in its bar) |

### `Lineup.Marking`

| Command | Parameters | Purpose |
|---|---|---|
| `ShowOpponentSquad` | – | Show Opponent Squad. See current Help for behaviour and bounds. |
| `SetMarking` | own:Player, opponent:Player | Set Marking. See current Help for behaviour and bounds. |
| `ClearMarking` | opponent:Player | Clear Marking. See current Help for behaviour and bounds. |

### `Lineup.SetPieces`

| Command | Parameters | Purpose |
|---|---|---|
| `ShowSetPieces` | – | Show Set Pieces. See current Help for behaviour and bounds. |
| `SetSetPieceTaker` | kind:enum(FreeKick,Penalty,Corner), player:Player | Set Set Piece Taker. See current Help for behaviour and bounds. |
| `SetSetPieceAuto` | [kind:enum(FreeKick,Penalty,Corner)] | Set Set Piece Auto. See current Help for behaviour and bounds. |

### `PlayerCard`

| Command | Parameters | Purpose |
|---|---|---|
| `ShowPlayer` | [player:Player] | the card (another player of the squad by name) |
| `OpenPlayer` | player:Player | another player's card |
| `OpenPlayerStatistics` | – | the development charts (own players) |
| `Negotiate` | – | Negotiate. See current Help for behaviour and bounds. |
| `NationalSquadAdd` | – | National Squad Add. See current Help for behaviour and bounds. |
| `NationalSquadRemove` | – | National Squad Remove. See current Help for behaviour and bounds. |

### `PlayerCard.Statistics`

| Command | Parameters | Purpose |
|---|---|---|
| `ShowPlayerStatistics` | [chart:enum(Value,Form,Strength,Total), mode:enum(Absolute,Relative)] | a development chart as a series |
| `SetChart` | chart:enum(Value,Form,Strength,Total) | SPIELER-WERT / FORM / STÄRKE / GESAMT |
| `SetMode` | mode:enum(Absolute,Relative) | ABS / REL |
| `BackToPlayerInfo` | – | back to the card |

### `Tactics`

| Command | Parameters | Purpose |
|---|---|---|
| `ShowTactics` | – | the eight settings with value and maximum |
| `SetTactic` | item:int(0..7), value:int | set a value directly (item 0 ends the assistant's D/O control) |
| `CycleTactic` | item:int(0..6) | a click: value + 1, after the maximum 0 |

### `Training`

| Command | Parameters | Purpose |
|---|---|---|
| `ShowTraining` | – | types, areas, sums, players, automation |
| `SetTrainingType` | type:enum(TOR,ABW,MIT,STU), balls:int(0..3) | balls of a player type (sum <= 8) |
| `SetTrainingArea` | area:enum(Fitness,Skill,Corner,FreeKick,Tackling,Pass,Shot,Speed,Game), balls:int(0..3) | balls of a training area (F+S <= 4, rest <= 18) |
| `SetPlayerIntensity` | player:Player, intensity:int(1..3) | training intensity of a player |
| `CyclePlayerFocus` | player:Player | the player's focus symbol: next focus |
| `SetTrainingAutomation` | index:int(0..4), on:bool | one automation bit (D p. 109) |
| `InitializeTraining` | – | the original initialization (only when needed) |

### `TrainingCamp`

| Command | Parameters | Purpose |
|---|---|---|
| `ListResorts` | – | the eight sports schools with stars and price |
| `ShowCamp` | – | sliders, recuperation, visits |
| `SetCampFocus` | fitness:int(0..15), skill:int(0..15), game:int(0..15) | the three sliders; recuperation follows |
| `BookCamp` | resort:int(0..7) | MACH ICH: book the sports school |

### `Transfer`

| Command | Parameters | Purpose |
|---|---|---|
| `ShowMarket` | [page:int] | the offered players (20 per page) |
| `SetMarketFilter` | positions:list(TOR,ABW,MIT,STU) \| All | ALLE / TOR / ABW / MIT / STU |
| `SetSquadView` | view:enum(Active,Team) | Set Squad View. See current Help for behaviour and bounds. |
| `SetSquadSheet` | sheet:enum(A,B) | Set Squad Sheet. See current Help for behaviour and bounds. |
| `SetMarketVisible` | on:bool | Set Market Visible. See current Help for behaviour and bounds. |
| `ShowPlayer` | player:Player | Show Player. See current Help for behaviour and bounds. |
| `SellPlayer` | player:Player | VER-KAUFEN: put an own player on the list |
| `LoanOutPlayer` | player:Player, months:int(12\|24) | VER-LEIHEN: offer an own player for loan |
| `BuyPlayer` | player:Player | VER-KAUFEN on a market player: the offer editor |
| `HirePlayer` | player:Player, months:int(12\|24) | VER-LEIHEN on a market player: the loan offer |
| `OpenOffers` | player:Player | Open Offers. See current Help for behaviour and bounds. |
| `OpenSearch` | – | Open Search. See current Help for behaviour and bounds. |
| `OpenPoach` | – | ABWERBEN: pick a player of another club (Transfer.Poach) |
| `ShowTransferNews` | [count:int] | Show Transfer News. See current Help for behaviour and bounds. |
| `ShowTeamInfo` | – | Show Team Info. See current Help for behaviour and bounds. |

### `Transfer.Negotiate`

| Command | Parameters | Purpose |
|---|---|---|
| `ShowNegotiation` | – | demand, quote and your offer |
| `SetOffer` | fee:money, salary:money, years:int(1..4), bonusGoal:int, bonusPoints:int, bonusAppearance:int, bonusChampion:int, bonus2to5:int, bonus6to10:int, bonusAvoidRelegation:int | all ten fields |
| `SetOfferField` | field:enum(Fee,Salary,Years,BonusGoal,BonusPoints,BonusAppearance,BonusChampion,Bonus2to5,Bonus6to10,BonusAvoidRelegation), value:int | change one field (a fresh quote follows) |
| `SubmitOffer` | – | ANGEBOT: hand in the offer |
| `ShowTeamInfo` | – | TEAM-INFO: your club's squad, as the editor shows it |
| `ShowPlayerInfo` | – | SPIELER-INFO: the player's card with his market value |

### `Transfer.Search`

| Command | Parameters | Purpose |
|---|---|---|
| `ShowSearchProfile` | – | Show Search Profile. See current Help for behaviour and bounds. |
| `SuggestProfile` | – | Suggest Profile. See current Help for behaviour and bounds. |
| `SetSearchProfile` | types:list(TOR,ABW,MIT,STU), age:range, strength:range, focus:list(Fitness,Skill,Form), side:enum(Left,Right,Both), style:enum(Defensive,Offensive,Both), price:enum(Min,Normal,Max) | Set Search Profile. See current Help for behaviour and bounds. |
| `StartSearch` | – | Start Search. See current Help for behaviour and bounds. |

### `Transfer.Poach`

| Command | Parameters | Purpose |
|---|---|---|
| `ListPoachClubs` | [league:int] | the clubs of a league (default: own) |
| `ListClubPlayers` | club:Club | the players of the club |
| `PoachPlayer` | player:Player | the offer to a player of another club (decided at once) |

### `Transfer.Offers`

| Command | Parameters | Purpose |
|---|---|---|
| `ListOffers` | [player:Player] | List Offers. See current Help for behaviour and bounds. |
| `AcceptOffer` | offer:int | Accept Offer. See current Help for behaviour and bounds. |
| `RejectOffer` | offer:int | Reject Offer. See current Help for behaviour and bounds. |
| `NegotiateOffer` | offer:int | Negotiate Offer. See current Help for behaviour and bounds. |
| `ChooseBuyer` | player:Player, offer:int | Choose Buyer. See current Help for behaviour and bounds. |

### `Contracts`

| Command | Parameters | Purpose |
|---|---|---|
| `ShowContracts` | [sheet:enum(A,B)] | the squad with salaries and contract months |
| `ShowTeamBonuses` | – | the seven team bonuses (percent of the salary) |
| `SetTeamBonus` | kind:enum(Goal,Points,Appearance,Champion,Place2to5,Place6to10,AvoidRelegation), percent:int | a team bonus; it can only be raised |
| `OpenNegotiation` | player:Player | the player's demands (Contracts.Negotiate) |

### `Contracts.Negotiate`

| Command | Parameters | Purpose |
|---|---|---|
| `ShowDemands` | – | the demands for 1-4 years |
| `SetContractOffer` | years:int(1..4), salary:money | the salary offer (monthly) |
| `SetInsurance` | form:enum(A,B,C,D), months:int | insure the player (no question) |
| `SetPlayerBonus` | kind:enum(Goal,Points,Appearance,Champion,Place2to5,Place6to10,AvoidRelegation), percent:int | a bonus for this player (PRÄMIEN) |
| `MakeOffer` | – | ANGEBOT MACHEN: the player decides |

### `Bank`

| Command | Parameters | Purpose |
|---|---|---|
| `ShowBank` | – | balance, savings, credit line, season base |
| `OpenAccount` | – | the finance overview (Bank.Account) |
| `OpenSavings` | – | the fixed deposit (Bank.Savings) |
| `OpenCredit` | [lender:enum(Bank,Manager1,Manager2,Manager3,Manager4)] | the credit view (Bank.Credit) |
| `OpenRealEstate` | – | real estate funds (Bank.RealEstate) |
| `OpenShares` | – | the stock market (Bank.Shares) |

### `Bank.Account`

| Command | Parameters | Purpose |
|---|---|---|
| `ShowFinanceOverview` | – | assets, debts and net worth |
| `ShowAssetProfitLoss` | asset:int(0..11) | profit/loss of a share |

### `Bank.Savings`

| Command | Parameters | Purpose |
|---|---|---|
| `ShowSavings` | – | account and fixed deposit |
| `Deposit` | amount:money(>0) | account -> fixed deposit |
| `Withdraw` | amount:money(>0) | fixed deposit -> account |

### `Bank.Shares`

| Command | Parameters | Purpose |
|---|---|---|
| `ListShares` | – | the twelve shares |
| `ShowShare` | asset:int(0..11) | one share with its last 30 prices |
| `ShareAdvice` | asset:int(0..11) | the bank's tip for a share |
| `BuyShares` | asset:int(0..11), count:int(>0) | buy shares |
| `SellShares` | asset:int(0..11), count:int(>0) | sell shares |

### `Bank.RealEstate`

| Command | Parameters | Purpose |
|---|---|---|
| `ListRealEstate` | – | the six real estate funds |
| `BuyRealEstate` | type:int(0..5), units:int(>0) | buy units |
| `SellRealEstate` | type:int(0..5), units:int(>0) | sell units |

### `Bank.Credit`

| Command | Parameters | Purpose |
|---|---|---|
| `ShowCredit` | [lender:enum(Bank,Manager1..4)] | the credit rows with the editable ranges |
| `SetCreditAmount` | amount:money | AUSGEZAHLTER BETRAG: the credit |
| `SetCreditPayment` | monthly:money | the monthly payment: within 20 % of the annuity for a credit from a manager |
| `SetCreditTerm` | months:int | LAUFZEIT in months |
| `RepayCredit` | amount:money \| All | RÜCKZAHLUNG: pay back (All = as much as possible) |
| `ConfirmCredit` | – | book the changes (asks about fees) |
| `CancelCredit` | – | discard the changes |

### `Stadium`

| Command | Parameters | Purpose |
|---|---|---|
| `ShowStadium` | – | facilities, maintenance and blocks |
| `ShowFutureExpansion` | – | Show Future Expansion. See current Help for behaviour and bounds. |
| `QuoteBlockExpansion` | block:enum(A..H), seats:int, seated:bool, roofed:bool | Quote Block Expansion. See current Help for behaviour and bounds. |
| `ConfirmBlockExpansion` | [tip:bool=false] | Confirm Block Expansion. See current Help for behaviour and bounds. |
| `DeclineBlockExpansion` | – | Decline Block Expansion. See current Help for behaviour and bounds. |
| `BuildFacility` | facility:enum(Floodlights,Scoreboard,PitchHeating,VipLounge) | technical expansion (AUSBAUEN): tier and tip questions |
| `Maintain` | item:enum(Pitch,Comfort,Condition,Safety) | maintenance: tier and tip questions |
| `SetTicketPrice` | price:int(8..25) | the ticket price (as on the desk) |
| `OpenPeriphery` | – | SICHT: PANORAMA (Stadium.Periphery) |
| `OpenData` | – | Open Data. See current Help for behaviour and bounds. |

### `Stadium.Periphery`

| Command | Parameters | Purpose |
|---|---|---|
| `ListPeriphery` | – | the 14 objects: built, building, days |
| `QuotePeriphery` | object:int(0..13) | the building firm's current offer (question) |
| `BuildPeriphery` | object:int(0..13), price:money | accept the offer |
| `DeclinePeriphery` | object:int(0..13), price:money | decline the offer |
| `OpenInterior` | – | SICHT: INNEN (back to Stadium) |

### `Stadium.Data`

| Command | Parameters | Purpose |
|---|---|---|
| `ShowStadiumData` | [chart:enum(Spectators,Admission,StadiumValue,ExtraIncome,Income,Costs), mode:enum(Absolute,Relative)] | Show Stadium Data. See current Help for behaviour and bounds. |

### `Advertising`

| Command | Parameters | Purpose |
|---|---|---|
| `ShowAdvertising` | – | all boards, shirt, TV and the spending |
| `ListAdOffers` | slot:enum(Board1..Board14,Shirt,Tv) | the offers for a board, the shirt or TV (an expired contract: renewal first) |
| `AcceptAdOffer` | slot:enum, offer:int | ANNEHMEN |
| `DeclineAdOffer` | – | ABBRECHEN |
| `SetSpending` | kind:enum(PublicRelations,FanCare), amount:money | monthly budget for press work or fan care |
| `BuyFanItems` | amount:money | buy fan articles (sold in 6-9 weeks) |

### `Environment`

| Command | Parameters | Purpose |
|---|---|---|
| `ShowEnvironment` | – | six monthly budgets and three youth tables |
| `SetStaffLevel` | role:enum(AssistantCoach,Doctor,Physio), level:int(0..6) | click a staff tile: level 0..6, 5 000 a month each |
| `SetYouthLevel` | team:enum(Youngest,Academy,Oldest), level:int(0..6) | set a youth team’s monthly budget |
| `SetYouthAcademy` | state:enum(On,Off) | compatibility alias: middle youth budget at level 6 or 0 |

### `Balance`

| Command | Parameters | Purpose |
|---|---|---|
| `ShowBalance` | – | all items, the view and the planned profit or loss |
| `SetBalanceView` | view:enum(SoFar,Real,Future) | left button: so far / real / future |
| `SetBalanceItem` | item:enum(IncomeShirt,IncomeBoards,IncomeTv,IncomePeriphery,IncomeRentInterest,IncomeMisc,ExpenseSalaries,ExpenseStadium,ExpenseYouth,ExpenseCoTrainer,ExpenseDoctor,ExpensePhysio,ExpenseTrainingCamp,ExpenseFanCare,ExpensePr,ExpenseInterest,ExpenseMisc,OtherHomeGames,AverageGate,TicketPrice), value:int | change a planned value (views Real and Future) |
| `TakeActuals` | – | "Werte akt.": drop the own changes, back to the game's figures |

### `Toto`

| Command | Parameters | Purpose |
|---|---|---|
| `ShowToto` | – | the eleven matches with your tips |
| `SetTip` | match:int(1..11), tip:enum(1,0,2) | the tip for one match |
| `SetStake` | amount:money(>=0) | the stake |
| `SubmitToto` | [stake:money, tips:list(11)] | MACH ICH: hand in the coupon |
| `ClearToto` | – | all tips back to 1, stake 0 |

### `Friendlies`

| Command | Parameters | Purpose |
|---|---|---|
| `ListFriendlyOpponents` | [league:int(0..8)] | the clubs of a league (default: own) |
| `ProposeFriendly` | club:Club, venue:enum(Home,Away) | the offer: costs and who pays |
| `ListFreeDates` | – | the days the friendly can be played |
| `SetFriendlyDate` | date:date | book the friendly on a free date (YYYY-MM-DD) |

### `Reschedule`

| Command | Parameters | Purpose |
|---|---|---|
| `ListRescheduleGames` | – | own home league matches that can be moved |
| `ListRescheduleDates` | game:date | the days the match can be moved to |
| `MoveGame` | game:date, to:date | move the match to a free day |
| `ListMovedGames` | – | moved matches that can still be taken back (new date) |
| `UndoMove` | game:date | take a move back (game = the new date); the match is played on its matchday again |
