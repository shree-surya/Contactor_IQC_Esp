# Google Sheets setup

The rig reads **Specs** and **Operators** from a Google Sheet and appends test results to its
**Results** tab. It signs in with a Google Cloud **service account** (JSON key), so no Apps
Script is needed.

Allow about 30 minutes. You only do this once.

---

## Part A: Create the Sheet from the template

1. Download [`Contactor_IQC_Sheet_Template.xlsx`](Contactor_IQC_Sheet_Template.xlsx) from this folder.
2. Open **drive.google.com**, then **New → File upload**, and pick the template.
3. Double-click the uploaded file. It opens in Google Sheets.
4. **File → Save as Google Sheets.** This step is required: the API can't write to an `.xlsx`.
   A new copy opens.
5. Rename that copy to **Contactor IQC** (top-left title) and delete the uploaded `.xlsx` from Drive.
6. Check it has 4 tabs: **Specs, Operators, Results, README**.
   - **Specs:** check the 4 models and their limits.
   - **Operators:** replace `Operator 1…3` with real names (max 10, one per row).
   - Don't rename tabs or edit header rows.
7. Copy the **Sheet ID** from the address bar and keep it for Part D:

   ```
   https://docs.google.com/spreadsheets/d/1AbCdEfGhIjKlMnOpQrStUvWxYz0123456789/edit#gid=0
                                          └──────────── this is the Sheet ID ───────┘
   ```

---

## Part B: Create the service account (Google Cloud)

1. Open **console.cloud.google.com** and sign in with the same Google account.
2. Top bar → project picker → **New Project**. Name it `Contactor-IQC` → **Create**, then select it.
3. **☰ Menu → APIs & Services → Library**. Search **Google Sheets API** → open it → **Enable**.
4. **☰ Menu → IAM & Admin → Service Accounts → + Create service account**.
   - Name: `iqc-rig` → **Create and continue**.
   - Roles: leave empty → **Continue** → **Done**.
5. Click the new account (`iqc-rig@contactor-iqc-xxxx.iam.gserviceaccount.com`) → **Keys** tab →
   **Add key → Create new key → JSON → Create**. A `.json` file downloads.
   - If you see *"Service account key creation is disabled"*, your company blocks keys by policy.
     Ask IT to allow it for this project (policy `iam.disableServiceAccountKeyCreation`).
6. **Keep this JSON file private.** Don't email it, upload it to GitHub, or send it in chat. Anyone
   with it can edit the Sheet.

---

## Part C: Share the Sheet with the service account

1. Open the JSON file in Notepad and copy the `client_email` value
   (ends in `.iam.gserviceaccount.com`).
2. In the **Contactor IQC** Sheet: **Share** → paste that email → role **Editor** →
   untick *Notify people* → **Share**.

---

## Part D: Put the key into the firmware

1. In `hmi/include/`, copy `secrets.example.h` and name the copy **`secrets.h`**.
   `secrets.h` is listed in `.gitignore`, so it never goes to GitHub.
2. Fill it from the JSON file and the Sheet ID:

   | `secrets.h` | Where it comes from |
   |---|---|
   | `GSHEET_ID` | Part A step 7 |
   | `GSA_PROJECT_ID` | JSON `project_id` |
   | `GSA_CLIENT_EMAIL` | JSON `client_email` |
   | `GSA_PRIVATE_KEY` | JSON `private_key`: copy everything between the quotes, **including** the `\n` sequences, exactly as it is |

3. Save. Don't commit. Run `git status` and check that `secrets.h` is **not** listed.

---

## Part E: Network check

From a laptop on the **same WiFi** the rig will use, open these in a browser:

- `https://oauth2.googleapis.com`
- `https://sheets.googleapis.com`

Any response (even "404" or "Not Found") means the network allows it. "Blocked", a login page,
or a timeout means IT has to allow those two hosts.

---

## When you're done

Tell me **"Sheets setup done"**. Don't send the key or the JSON. I'll then add the Sheets code to
the firmware:

- download Specs + Operators at boot and on **SYNC NOW**, and cache them in flash for offline use
- append one Results row per channel per cycle
- queue rows in flash while WiFi is down and upload them later
- timestamp from Google's clock, `*` when the rig was offline
