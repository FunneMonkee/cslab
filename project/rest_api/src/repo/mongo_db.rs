extern crate dotenv;

use std::env;

use mongodb::{
    bson::{doc, oid::ObjectId, to_bson},
    error::Error,
    results::{DeleteResult, UpdateResult},
};
use mongodb::{
    results::InsertOneResult,
    sync::{Client, Collection},
};

use crate::model::dispense_log::DispenseLog;
use crate::model::user::User;

pub struct MongoRepo {
    col: Collection<User>,
    dispense_log_col: Collection<DispenseLog>,
}

impl MongoRepo {
    pub fn init() -> Self {
        let uri = match env::var("MONGOURI") {
            Ok(var) => var.to_string(),
            Err(_) => format!("Error when getting env var!"),
        };
        let client = Client::with_uri_str(uri).unwrap();
        let db = client.database("cslab");
        let col: Collection<User> = db.collection("users");
        let dispense_log_col: Collection<DispenseLog> = db.collection("dispense_log");
        MongoRepo {
            col,
            dispense_log_col,
        }
    }

    pub fn create_user(&self, new_user: User) -> Result<InsertOneResult, Error> {
        match self.col.insert_one(new_user, None) {
            Ok(result) => Ok(result),
            Err(e) => {
                eprintln!("Error creating user: {:?}", e);
                Err(e)
            }
        }
    }

    pub fn get_user(&self, id: &String) -> Result<Vec<User>, Error> {
        let filter = doc! {"nfc_id": id};

        let cursors = self
            .col
            .find(filter, None)
            .ok()
            .expect("Error getting list of users");
        let users = cursors.map(|doc| doc.unwrap()).collect();
        Ok(users)
    }

    pub fn update_user(&self, id: &String, new_user: User) -> Result<UpdateResult, Error> {
        let obj_id = ObjectId::parse_str(id).unwrap();
        let filter = doc! {"_id": obj_id};
        let new_doc = doc! { "$set": to_bson(&new_user).unwrap() };

        let updated_doc = self
            .col
            .update_one(filter, new_doc, None)
            .ok()
            .expect("Error updating user");
        Ok(updated_doc)
    }

    pub fn delete_user(&self, id: &String) -> Result<DeleteResult, Error> {
        let obj_id = ObjectId::parse_str(id).unwrap();
        let filter = doc! {"_id": obj_id};

        let deleted_user = self
            .col
            .delete_one(filter, None)
            .ok()
            .expect("Error deleting user");
        Ok(deleted_user)
    }

    pub fn create_dispense_log(&self, log: DispenseLog) -> Result<InsertOneResult, Error> {
        self.dispense_log_col.insert_one(log, None)
    }

    pub fn get_all_dispense_logs(&self) -> Result<Vec<DispenseLog>, Error> {
        let cursor = self.dispense_log_col.find(None, None)?;
        Ok(cursor.map(|doc| doc.unwrap()).collect())
    }
}
