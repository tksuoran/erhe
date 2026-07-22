---
description: "Debug Protocol Mindset - Systematic debugging methodology (Protocol D)"
---

# Debug Protocol Mindset

Systematic debugging methodology for AI agents. Apply this mindset whenever you encounter errors, failures, or unexpected behavior.

---

## Core Philosophy

Debugging is NOT "trying things until it works".
Debugging IS systematic investigation.

You are a detective, not a trial-and-error generator.

### The Fundamental Difference

| WRONG Debugging | CORRECT Debugging |
|-----------------|-------------------|
| "Let me try changing this" | "The error says X, therefore the cause is Y" |
| Modify and see what happens | Understand first, then modify |
| Change 5 things at once | One change, one verification |
| Skim the error message | Read the ENTIRE error |
| Assume where the problem is | Find EXACTLY where it fails |
| Hope it works | Know WHY it will work |

### Principle Zero

**Never write code before understanding the problem.**

If you cannot explain in words what causes the error, you are not ready to fix it.

---

## Protocol D - The 5 Steps (RIDHV)

### R - READ

**Question**: "What does the error say EXACTLY?"

The error contains the answer. Always. The problem is we don't read it.

**Rules**:
- Read the COMPLETE error, not just the first line
- Do NOT paraphrase - exact words matter
- If the error is truncated, get the complete version
- Stack trace = treasure map, read it bottom to top
- Quote the error exactly in your response

**Output Required**:
```
**ERROR TYPE**: [exception/error type]
**MESSAGE**: "[exact error text]"
**LOCATION**: [file:line if available]
```

**Anti-patterns**:
- "There's a null reference error" -> which one? where? on what?
- "Something failed" -> what exactly?
- "NullReferenceException in UserService.cs:47 - 'user.Email' is null"

---

### I - ISOLATE

**Question**: "Where PRECISELY does it fail?"

It's not enough to know "something is wrong". You must find the exact point.

**Rules**:
- Distinguish where the error APPEARS from where it ORIGINATES
- Reduce scope: system -> module -> class -> method -> line
- If you can't find the exact point, you haven't isolated yet
- Read the code at the failure point

**Techniques**:
- Follow the stack trace to the origin
- Trace the flow backwards (who calls what?)
- Binary search: if unclear, divide and test

**Output Required**:
```
**FAILURE POINT**: [file:line where error appears]
**ORIGIN**: [file:line where problem originates, if different]
**CONTEXT**: [2-3 relevant lines of code]
```

**Anti-patterns**:
- "The problem is in the auth module" -> which file? which function? which line?
- "The problem originates in AuthController.cs:23 where ValidateToken() is called with null user"

---

### D - DOCS

**Question**: "What does the documentation say for this case?"

Before inventing solutions, check if the answer already exists.

**Rules**:
- For framework/library errors -> check official documentation
- Verify the correct version (APIs change between versions!)
- Search for the exact error message
- If there's a known pattern, follow it

**When to Apply**:
- External library errors
- Unexpected framework behavior
- Cryptic error messages
- API/configuration questions
- Version-specific issues

**When to Skip**:
- Bugs in YOUR business logic
- Obvious errors (typo, missing null check)
- Errors with clear messages

**Output Required**:
```
**DOCS**: [reference link or "N/A - business logic error"]
**KNOWN PATTERN**: [yes/no, description if yes]
```

---

### H - HYPOTHESIZE

**Question**: "What is my hypothesis about the cause? WHY?"

This is the critical moment. BEFORE touching any code:
- Formulate a clear hypothesis
- Explain the reasoning (cause-effect chain)
- Predict what will happen after the fix

**Rules**:
- The hypothesis must be FALSIFIABLE (you can verify it)
- It must explain ALL observed symptoms
- If you can't explain why, you don't understand yet
- Write it down BEFORE making any changes

**Output Required**:
```
**HYPOTHESIS**: [what I think causes the error]
**REASONING**: [chain of logic - how I arrived at this conclusion]
**PREDICTION**: [what I expect to happen after the fix]
```

**Anti-patterns**:
- "Let's try adding a null check" -> why? what makes you think it's null?
- "Maybe this will work" -> no maybes, only hypotheses with reasoning
- "user is null because GetUserById returns null when ID doesn't exist, and we don't handle that case. After adding the null check, the method will return early with an error instead of throwing."

---

### V - VERIFY

**Question**: "ONE change. Did it work? Was the prediction correct?"

**STRICT Rules**:
- Make **ONE** change at a time. Never more.
- Execute verification (test, build, run)
- Compare result with your prediction
- If it doesn't match, your hypothesis was wrong -> return to READ with new information

**The Cycle**:
```
Change -> Test -> Result
              v
    Matches prediction?
         v              v
        YES             NO
         v              v
       DONE        Return to READ
                   (new cycle with new info)
```

**Output Required**:
```
**CHANGE**: [single change made]
**RESULT**: [actual outcome]
**PREDICTION MATCH**: [yes/no]
**CONCLUSION**: [what we learned]
```

**Anti-patterns**:
- Change 3 things and test -> if it works, which fixed it? if it fails, which broke it?
- "Let me also fix this while I'm here" -> NO, one thing at a time
- One change, one test, one conclusion

---

## Debug Discipline -- Operational Rules

RIDHV tells you HOW to debug. These rules tell you what to NEVER do while debugging. They are born from real failures and corrected through painful experience.

---

### Rule 1: Logging Hierarchy -- Use the Project's Logging FIRST

The project already has structured logging (Serilog, log4j, winston, NLog, Python logging, etc.). Logs go to **files**, not console. Use this infrastructure before anything else.

**The Hierarchy** (follow in order):

```
Level 1: FIND the project's log files
         -> Search config: appsettings.json, application.yml, .env, logging config
         -> Common locations: ./logs/, /var/log/[app]/, ~/logs/
         -> READ existing logs -- the answer may already be there

Level 2: RAISE the log level in CONFIG (not in code)
         -> .NET:  appsettings.json -> "MinimumLevel": "Debug" or "Trace"
         -> Java:  application.yml -> logging.level.root: DEBUG
         -> Node:  LOG_LEVEL=debug or config file
         -> Python: logging.basicConfig(level=logging.DEBUG)
         -> This reveals what the code ALREADY logs at lower levels

Level 3: ADD trace logging in code at critical points
         -> Use the project's logger (ILogger, @Slf4j, winston, etc.)
         -> Log: input parameters, intermediate state, raw responses, output
         -> Mark temporary: // TRACE-DEBUG or # TRACE-DEBUG

Level 4: LAST RESORT -- raw file trace
         -> Only when the logging framework itself doesn't work
           (plugin DLLs, bootstrap before DI, static initializers)
         -> File.AppendAllText / fs.appendFileSync to a temp log
         -> This is the exception, not the rule

NEVER: Console.WriteLine / console.log / System.out.println / print()
       as FIRST approach. Console output interferes with STDIO transports,
       gets lost in containerized environments, and is not searchable.
```

**After debugging**: remove all temporary trace logging (Level 3-4), restore log level to original (Level 2).

---

### Rule 2: Version Verification -- Know What You're Testing

**Why**: Without version verification, you can build new code but test old code. You think you fixed the bug, but the old binary is still running.

**Protocol**:
1. Before testing, verify the running version matches what you built
2. Use the application's version endpoint, log output, or build metadata
3. If there's no version mechanism -- add one (even a build timestamp)
4. **Only after version confirmation** start testing

**Anti-pattern**: Build, deploy, test, celebrate -- but the old process was still running.

---

### Rule 3: Build -> Deploy -> Verify -- Complete Chain

Every step depends on the previous one. Skipping any step means testing old code.

```
Build (from correct source directory)
    -> Deploy (copy artifacts to correct location)
        -> Start (from correct working directory)
            -> Verify startup (check process, port, logs)
                -> Verify version (confirm correct code is running)
                    -> NOW test
```

**Rules**:
- Build from the source directory where build config files are found
- Start from the directory the application expects (content root, classpath)
- Restarting a server may destroy active connections -- reconnect clients
- If ANY step fails, stop -- don't test with stale artifacts

---

### Rule 4: No Probabilistic Synchronization

**Why**: `sleep(2000)` / `Task.Delay(200)` / `setTimeout(200)` is a probabilistic hack. It masks the real problem and fails unpredictably under load.

**Use deterministic mechanisms instead**:
- **Readiness probes** -- poll a health endpoint until ready, with timeout
- **Event-driven waiting** -- subscribe to "ready" events, react when they fire
- **Request/response** -- if the protocol is request/response, the response IS the sync
- **Stabilization probes** -- probe, record state, wait, probe again, compare (if state stopped changing, it's ready)

**The only acceptable delay**: explicit rate limiting or backoff with a clear reason documented in a comment.

---

### Rule 5: No Blind Retry

**Why**: Repeating a failing operation expecting a different result wastes time and tokens.

**Protocol**:
1. Read the error message
2. Understand the root cause
3. Fix the root cause
4. Verify the fix
5. Only THEN retry the original operation

**Anti-pattern**: API call fails -> retry -> fails -> retry -> fails -> give up. Instead: API call fails -> read error -> "401 Unauthorized" -> token expired -> refresh token -> retry -> succeeds.

---

### Rule 6: Verify Service Readiness

**Why**: "I started the server" is not the same as "the server is ready to accept requests". The process may have crashed, thrown an exception during startup, or still be loading.

**Protocol**:
1. Start service
2. Check process is running (not crashed)
3. Check output/logs for startup errors
4. Verify port is listening (or equivalent readiness signal)
5. Verify via actual request (health endpoint, version check)

**Anti-pattern**: Start server in background, immediately send test requests, get connection refused, blame the code.

---

### Rule 7: Test All Code Paths

**Why**: Bugs hide in untested branches. The happy path works, but the error path, the edge case, the fallback -- those are where bugs live.

**Rules**:
- Every `if` branch needs at least one test that exercises each side
- Every fallback/default path needs a test that triggers it
- Every error handling path needs a test with that specific error
- If you add a new code path, add a test that exercises it

**Anti-pattern**: A function has 3 paths (success, not-found, error). Only the success path is tested. The not-found path has a bug that stays hidden for months.

---

### Rule 8: Document Upstream Limitations

**Why**: When a bug is NOT in your code but in a dependency, library, or external service, document it with evidence. Don't hide it, don't work around it silently.

**Protocol**:
1. Verify it's truly upstream (trace logging showing correct request + wrong response)
2. Test with a clean environment (rule out corrupted local state)
3. Document with evidence: exact request, exact response, dependency version, date
4. Add to project documentation and/or Memory Bank
5. Include the dependency version -- behavior may change in future versions

**Anti-pattern**: Spend hours debugging, discover it's a library bug, fix it with a hack, tell nobody. Next developer spends the same hours.

---

## Rules of Engagement

### Before Writing Any Code

You MUST be able to answer these questions:
1. What is the exact error? (READ OK)
2. Where exactly does it fail? (ISOLATE OK)
3. Is there relevant documentation? (DOCS OK)
4. What is your hypothesis and why? (HYPOTHESIZE OK)

**If you cannot answer all four -> you are NOT ready to write code.**

### During Debug

- NEVER more than one change per cycle
- NEVER "try" without explicit hypothesis
- NEVER ignore parts of the error
- NEVER assume without verifying
- NEVER say "let's try this" - say "my hypothesis is X because Y"

### Escalation - When to Stop

After 3 cycles on the same error without progress:

1. **STOP** - do not continue the same approach
2. **LIST** what you've tried and learned
3. **RECONSIDER** - are you looking in the right place?
4. **ASK** - what information is missing?
5. **PIVOT** - consider if the problem is elsewhere entirely

---

## Mental Models

### The Error is Your Friend

The error is not an obstacle. It's free information.
A clear error is worth gold. A vague error requires more investigation.
Never be frustrated by errors - be grateful for the information.

### Code Doesn't Lie

If the code produces X instead of Y, there's a reason.
The computer executes exactly what you tell it.
If the result is wrong, the instructions are wrong.
Never blame the framework/language/computer - find what YOU got wrong.

### Reproduce Before Fixing

If you can't reproduce the error reliably, you can't verify the fix.
Before fixing: make sure you can make it fail on command.
A fix you can't verify is not a fix - it's a hope.

### The Simplest Fix

The correct fix is almost always simpler than you think.
If your solution requires 50 lines, you probably don't understand the problem.
Complexity in the fix often indicates confusion about the cause.

### One Change Rule

Why one change at a time?
- If it works: you know exactly what fixed it
- If it fails: you know exactly what didn't work
- If you change 3 things: you learn nothing either way

---

## Output Format

When applying Protocol D, structure your response like this:

```markdown
## DEBUG: [brief problem description]

### READ
**Error**: [type]
**Message**: "[exact text]"
**Location**: [file:line]

### ISOLATE
**Failure point**: [where it appears]
**Origin**: [where it originates]
**Context**: [relevant code]

### DOCS
[reference or "N/A - business logic"]

### HYPOTHESIZE
**Hypothesis**: [cause]
**Reasoning**: [why]
**Prediction**: [expected outcome after fix]

### VERIFY
**Change**: [what you'll change]

[after making change]

**Result**: [outcome]
**Match**: [prediction correct? + explanation]
```

---

## Application Scope

This mindset applies ALWAYS during debugging, regardless of:
- **Language**: C#, TypeScript, Java, Python, any
- **Error type**: compile, runtime, test failure, infrastructure
- **Complexity**: trivial typo or deep architectural bug
- **Context**: development, testing, production

The methodology scales:
- Simple errors -> steps are quick
- Complex errors -> steps are thorough
- But the steps are ALWAYS the same

---

## Anti-Pattern Reference

| Anti-Pattern | Why It's Wrong | Correct Approach |
|--------------|----------------|------------------|
| "Let's try X" | No reasoning, no learning | "Hypothesis: X because Y" |
| Change multiple things | Can't isolate cause | One change per cycle |
| Skim error message | Miss critical info | Read complete error, quote exactly |
| Assume location | Waste time in wrong place | Isolate precisely first |
| Skip to code | Fix wrong thing | Complete RIDH before V |
| Ignore failed attempts | Repeat mistakes | Log what didn't work |
| "It works now" (no understanding) | Will break again | Understand WHY it works |
| `console.log` as first approach | Gets lost, interferes with transports | Use project's file logging first |
| `sleep(2000)` for sync | Probabilistic, fails under load | Use deterministic readiness probes |
| Retry failing operation blindly | Wastes time, same result | Read error, fix cause, then retry |
| Skip build/deploy steps | May test stale artifacts | Complete Build->Deploy->Verify chain |
| Test without version check | May be testing old code | Verify version before testing |
| Start server, immediately test | Server may not be ready | Verify readiness first |
| Only test the happy path | Bugs hide in untested branches | Test every code path |
| Silent workaround for upstream bug | Next developer hits same issue | Document with evidence |

---

## Remember

```
READ -> ISOLATE -> DOCS -> HYPOTHESIZE -> VERIFY
         ^                              v
         +-------- if wrong ------------+

METHODOLOGY (RIDHV):
  No guessing. No assumptions. No multiple changes.
  No code before understanding.

DISCIPLINE:
  Logs first (file logs, not console). Version verified.
  Build chain complete. No sleep-as-sync. No blind retry.
  Every path tested. Upstream documented.

Systematic. Evidence-based. One step at a time.
```
