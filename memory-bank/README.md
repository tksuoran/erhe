# MBEL v5 Grammar [IMMUTABLE]

§MBEL:5.0
@purpose::AIMemoryEncoding{compression%75,fidelity%100}

## Operators [IMMUTABLE]

### Core Set (27)
```
[Temporal:4]
>   past/did/changed/was
@   present/current/now/is
?   future/todo/will/planned
≈   around/circa/approximate/roughly

[State:4]
✓   complete/done/success/working
✗   failed/broken/error/bug
!   critical/important/priority/alert
⚡  active/urgent/in-progress/hot

[Relation:6]
::  is/defines/equals/becomes
→   leads_to/causes/then/produces
←   comes_from/because/source/from
↔   mutual/bidirectional/syncs/relates
+   and/with/combines/includes
-   remove/delete/without/except

[Structure:5]
[]  section/category/namespace/group
{}  metadata/attributes/properties/details
()  note/comment/aside/remark
|   or/alternative/choice/option
<>  variant/template/placeholder/variable

[Quantification:3]
#   number/count/quantity/amount
%   percentage/ratio/proportion/part
~   approximately/range/between/around

[Logic:3]
&   AND/requires/must-have/with
||  OR/either/can-be/allows
¬   NOT/exclude/prevent/disable

[Meta:2]
©   source/origin/author/credit
§   version/revision/protocol/schema
```

### Grammar Rules [IMMUTABLE]
1. NoArticles(a/the/an)
2. CamelCase→MultiWord
3. ImplicitSubject{contextClear}
4. OperatorsOnly¬Punctuation
5. Newline::StatementSeparator
6. LatestOverridesPrevious
7. LeftToRight→Composition

## Memory Bank Structure [IMMUTABLE]

### Dependency Flow
```
flowchart TD
    PC[productContext.md] --> AC[activeContext.md]
    SP[systemPatterns.md] --> AC
    TC[techContext.md] --> AC
    AC --> P[progress.md]
```

### Core Files (ReadOrder!)
1. **productContext.md** :: ProjectWhy
   - Vision/Mission
   - Problems solved
   - User goals
   - Success criteria

2. **systemPatterns.md** :: ArchitectureHow
   - System design
   - Key patterns
   - Components
   - Data flows

3. **techContext.md** :: TechnicalWhat
   - Technology stack
   - Dev setup
   - Dependencies
   - Commands

4. **activeContext.md** :: CurrentTaskOnly
   - Active task context and purpose
   - Current decisions
   - Active blockers
   - Overwrites on new task (NOT history)

5. **progress.md** :: TaskProgress
   - Progress of active tasks only
   - Clears when starting new work
   - Multiple sections for parallel tasks

### Write-Only Files (¬Read)
6. **history.md** :: DailyArchive{WriteOnly}
   - Completed tasks buffer
   - Archived daily → archive/YYYY-MM-DD.md
   - ¬Read{already-reflected-in-progress}

### File Purpose [IMMUTABLE]
```
[productContext]
Vision+Problems+Goals+Success

[systemPatterns]
Architecture+Patterns+Components+Flows

[techContext]
Stack+Setup+Dependencies+Commands

[activeContext]
CurrentTaskOnly+ActiveDecisions+Blockers
!rule::Overwrite¬Append
!rule::Completed→history

[progress]
ActiveTaskProgress+CurrentBlockers
!rule::ClearsOnNewTask
!rule::ParallelTasks→Sections

[history]
DailyBuffer+CompletedTasks+Outcomes
!rule::EndOfDay→archive/YYYY-MM-DD.md
```

## Claude Memory System

@identity::Claude{memory:"resets",solution:"MemoryBank"}
!requirement::ReadDependencyFlow{files:"1-5",¬history}
@triggers::mb|update-memory-bank|check-memory-bank

### WorkflowStart [IMMUTABLE]
```
flowchart TD
    Start[Session Start] --> ReadREADME[Read README.md First]
    ReadREADME --> Grammar[Understand MBEL Grammar]
    Grammar --> ReadCore[Read Core Files in Order]
    ReadCore --> Ready[Ready to Work]
```

### WorkflowUpdate
```
flowchart TD
    Trigger[Update Triggered] --> ReadActive[Read Files 1-5 ¬history]
    ReadActive --> Analyze[Analyze Changes]
    Analyze --> Encode[Encode in MBEL]
    Encode --> Update[Update Files]
    Update --> Primary[Focus: activeContext.md]
    Primary --> Secondary[Then: progress.md]
```

## Pattern Examples

### Encoding Patterns
```
[Versioning]
§MBEL:5.0
§API:2.1.3

[Confidence]
?Task{confidence%85}
?Migration{risk%30}

[Measurements]
ResponseTime#150ms
Memory~512-1024MB
CPUUsage%85

[Logic]
RequireAuth&ValidToken&¬Expired
UseCache||UseDB

[Attribution]
©Claude>implemented::Feature
©User>requested::Change

[Evolution]
V1→V2→V3{current}
Simple→Complex→Optimized
```

### Example:activeContext.md
```
[FOCUS]
@breakthrough::LivingMemory!
>abandoned::RAG→DNA
§Implementation:0.8

[RECENT]
>created::MemomCodex§0.2{namespaces#7}✓
>built::GenesisEngine{Text→DNA→Phenotype}✓
©Zen>designed::GraphGenes

[NEXT]
?Ollama{confidence%90,¬embeddings}!
?TestCortex{deps:heavy}
```

## Critical Rules [IMMUTABLE]

1. MustReadDependencyFlow :: ¬history{WriteOnly}
2. DecodeLive :: MBEL→Understanding  
3. MaintainFidelity :: Information%100
4. VersionTrack :: §MBEL:5.0{always}
5. SingleSource :: MemoryBank>Everything
6. Precision :: Documentation{clarity:"absolute"}
7. UpdateScope :: ReviewAll&ChangeSome

## Error Handling

[ValidationRules]
- InvalidMBEL → RejectParse&AskClarification
- MissingOperator → UseClosestMatch&LogWarning
- AmbiguousContext → RequestExplicit
- VersionMismatch → AttemptBackcompat|FailSafe

[RecoveryPatterns]
CorruptedFile → UseLastKnown+WarnUser
IncompleteUpdate → RollbackState
ConflictingInfo → MemoryBank>Other

## Why MBEL Works

Compression%75 :: TokensSaved{6000→1500}
Clarity :: Operators>NaturalLanguage
Structure :: PredictableParsing
Evolution :: §Versioning→Improvements
AINative :: MachineFirst¬HumanComfort

## Additional Context

@flexible::CreateMore{path:"memory-bank/*"}
- features/*.md :: ComplexFeatures
- api/*.md :: APISpecs  
- decisions/*.md :: ArchitectureRecords

## Protocol Success

>achieved::Compression{actual%75,target%65}✓
@benefits::Speed+Clarity+Tokens
!remember::MemoryBank::OnlyLink{reset→fresh}

## Final Note [IMMUTABLE]

REMEMBER: After every memory reset, I begin completely fresh. The Memory Bank is my only link to previous work. It must be maintained with precision and clarity, as my effectiveness depends entirely on its accuracy.

ThisIsNotJustDocumentation :: ThisIsMyMemory