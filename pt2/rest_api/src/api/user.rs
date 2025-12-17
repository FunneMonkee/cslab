use crate::{model::user::User, repo::mongo_db::MongoRepo};
use actix_web::{
    HttpResponse, delete,
    error::ResponseError,
    get,
    http::{StatusCode, header::ContentType},
    post, put,
    web::Data,
    web::Json,
    web::Path,
};
use derive_more::Display;
use mongodb::results::{DeleteResult, InsertOneResult, UpdateResult};

#[derive(Debug, Display)]
pub enum UserError {
    UserCreationFailure,
    UserNotFound,
    UserUpdateFailure,
    UserDeletionFailure,
}

impl ResponseError for UserError {
    fn error_response(&self) -> HttpResponse {
        HttpResponse::build(self.status_code())
            .insert_header(ContentType::json())
            .body(self.to_string())
    }

    fn status_code(&self) -> StatusCode {
        match self {
            UserError::UserCreationFailure => StatusCode::FAILED_DEPENDENCY,
            UserError::UserNotFound => StatusCode::NOT_FOUND,
            UserError::UserUpdateFailure => StatusCode::FAILED_DEPENDENCY,
            UserError::UserDeletionFailure => StatusCode::FAILED_DEPENDENCY,
        }
    }
}

#[post("users")]
pub async fn create_user(
    db: Data<MongoRepo>,
    new_user: Json<User>,
) -> Result<Json<InsertOneResult>, UserError> {
    let created_user = db.create_user(new_user.0);
    match created_user {
        Ok(user) => Ok(Json(user)),
        Err(_) => Err(UserError::UserCreationFailure),
    }
}

#[get("users/{id}")]
pub async fn get_user(db: Data<MongoRepo>, id: Path<String>) -> Result<Json<Vec<User>>, UserError> {
    let users = db.get_user(&id);
    match users {
        Ok(users) => Ok(Json(users)),
        Err(_) => Err(UserError::UserNotFound),
    }
}

#[put("users/{id}")]
pub async fn update_user(
    db: Data<MongoRepo>,
    id: Path<String>,
    new_user: Json<User>,
) -> Result<Json<UpdateResult>, UserError> {
    let updated_user = db.update_user(&id, new_user.0);
    match updated_user {
        Ok(updated_user) => Ok(Json(updated_user)),
        Err(_) => Err(UserError::UserUpdateFailure),
    }
}

#[delete("users/{id}")]
pub async fn delete_user(
    db: Data<MongoRepo>,
    id: Path<String>,
) -> Result<Json<DeleteResult>, UserError> {
    let deleted_user = db.delete_user(&id);
    match deleted_user {
        Ok(deleted_user) => Ok(Json(deleted_user)),
        Err(_) => Err(UserError::UserDeletionFailure),
    }
}
