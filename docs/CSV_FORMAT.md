# Participant CSV Format

JAYCEE Lottery accepts UTF-8 comma-separated value (`.csv`) files. The included [`participants-template.csv`](../assets/participants-template.csv) is ready to edit.

## Recommended columns

```csv
ticket,name,group
1001,Alyssa Hart,Marketing
1002,Benjamin Lee,Operations
1003,Camila Reyes,Marketing
```

| Column | Required | Description |
| --- | --- | --- |
| `ticket` | Recommended | Positive coupon number shown with the participant. Duplicate, missing, or invalid values are replaced with generated unique numbers. |
| `name` | Yes | Participant name. Rows with an empty name are skipped. |
| `group` | Optional | Department, category, or eligibility group. Empty values become `General`. |

Recognized header aliases are:

- Ticket: `ticket`, `coupon`, `number`, or `no`
- Name: `name` or `participant`
- Group: `group`, `department`, or `category`

Headers are matched without regard to capitalization. The recommended column order is `ticket,name,group`.

## Quoted values

Wrap a field in double quotes when it contains a comma:

```csv
ticket,name,group
1001,"Hart, Alyssa",Marketing
```

Use two double quotes to include a quote inside a quoted field:

```csv
1002,"Benjamin ""Ben"" Lee",Operations
```

## Eligibility groups

Every distinct group becomes a prize eligibility option on the **Prizes** page. For example, a prize restricted to `Marketing` will draw only rows whose group is exactly `Marketing`.

Keep group spelling and capitalization consistent across the file. Use an unrestricted prize when everyone should be eligible.

## Import behavior

- Importing a valid CSV switches the application to participant mode.
- The participant count becomes the draw pool size.
- Importing a replacement list clears the previous no-repeat pool.
- Invalid rows are skipped; the import fails only when no valid names remain.
- A UTF-8 byte-order mark is accepted.
